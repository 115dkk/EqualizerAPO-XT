/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <exception>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Shlwapi.h>
#include <Ks.h>
#include <KsMedia.h>
#include <mpParser.h>
#include <mpPackageCommon.h>
#include <mpPackageNonCmplx.h>
#include <mpPackageStr.h>
#include <mpPackageMatrix.h>

#include "helpers/RegistryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/ChannelHelper.h"
#include "ConfigurationFileReader.h"
#include "FilterEngine.h"
#include "filters/ExpressionFilterFactory.h"
#include "filters/DeviceFilterFactory.h"
#include "filters/StageFilterFactory.h"
#include "filters/IfFilterFactory.h"
#include "filters/ChannelFilterFactory.h"
#include "filters/BiQuadFilterFactory.h"
#include "filters/IIRFilterFactory.h"
#include "filters/PreampFilterFactory.h"
#include "filters/DelayFilterFactory.h"
#include "filters/CopyFilterFactory.h"
#include "filters/IncludeFilterFactory.h"
#include "filters/ConvolutionFilterFactory.h"
#include "filters/GraphicEQFilterFactory.h"
#include "filters/VSTPluginFilterFactory.h"
#include "filters/loudnessCorrection/LoudnessCorrectionFilterFactory.h"

using std::exception;
using std::find;
using std::lock_guard;
using std::make_unique;
using std::max;
using std::move;
using std::mutex;
using std::string;
using std::stringstream;
using std::swap;
using std::thread;
using std::unique_lock;
using std::vector;
using std::wstring;
using namespace mup;

void FilterDeleter::operator()(IFilter* filter) const
{
	if (filter == nullptr)
		return;

	filter->~IFilter();
	MemoryHelper::free(filter);
}

void FilterEngine::FilterConfigurationDeleter::operator()(FilterConfiguration* config) const
{
	if (config == nullptr)
		return;

	config->~FilterConfiguration();
	MemoryHelper::free(config);
}

FilterEngine::FilterEngine()
	: allocatedFrameCount(0),
	  preMix(false),
	  capture(false),
	  postMixInstalled(true),
	  inputChannelCount(0),
      realChannelCount(0),
      outputChannelCount(0),
	  lastInputWasSilent(false),
	  loadPermitAvailable(true),
	  shutdownRequested(false),
	  transitionCounter(0)
{
	parser = make_unique<ParserX>();
	parser->EnableAutoCreateVar(true);

	factories.push_back(make_unique<DeviceFilterFactory>());
	factories.push_back(make_unique<IfFilterFactory>());
	factories.push_back(make_unique<ExpressionFilterFactory>());
	factories.push_back(make_unique<IncludeFilterFactory>());
	factories.push_back(make_unique<StageFilterFactory>());
	factories.push_back(make_unique<ChannelFilterFactory>());
	factories.push_back(make_unique<IIRFilterFactory>());
	factories.push_back(make_unique<BiQuadFilterFactory>());
	factories.push_back(make_unique<PreampFilterFactory>());
	factories.push_back(make_unique<DelayFilterFactory>());
	factories.push_back(make_unique<CopyFilterFactory>());
	factories.push_back(make_unique<ConvolutionFilterFactory>());
	factories.push_back(make_unique<GraphicEQFilterFactory>());
	factories.push_back(make_unique<VSTPluginFilterFactory>());
	factories.push_back(make_unique<LoudnessCorrectionFilterFactory>());
}

FilterEngine::~FilterEngine()
{
	// Make sure notification thread is terminated before cleaning up, otherwise deleted memory might be accessed in loadConfig
	if (notificationWorker.joinable())
	{
		{
			lock_guard<mutex> lock(loadPermitMutex);
			shutdownRequested = true;
			loadPermitAvailable = true;
		}
		loadPermitCv.notify_all();
		shutdownEvent->set();
		notificationWorker.join();
		TraceF(L"Successfully terminated directory change notification thread");
		shutdownEvent.reset();
	}

	cleanupConfigurations();
}

void FilterEngine::resizeBuffers(unsigned frameCount) {
	if (allocatedFrameCount < frameCount || inputBuf2D.size() != inputChannelCount || outputBuf2D.size() != outputChannelCount) {

		TraceF(L"Reallocating internal double-precision buffers for %u frames and %u/%u channels.", frameCount, inputChannelCount, outputChannelCount);
		allocatedFrameCount = frameCount;

		// Resize 1D buffers (for interleaved audio)
		try {
			inputBuf1D.resize(inputChannelCount * frameCount);
			outputBuf1D.resize(outputChannelCount * frameCount);

			// Resize 2D buffers (for non-interleaved audio)
			inputBuf2D.resize(inputChannelCount);
			inputBuf2DPtrs.resize(inputChannelCount);
			for (unsigned i = 0; i < inputChannelCount; ++i) {
				inputBuf2D[i] = make_unique<double[]>(frameCount);
				inputBuf2DPtrs[i] = inputBuf2D[i].get();
			}
			outputBuf2D.resize(outputChannelCount);
			outputBuf2DPtrs.resize(outputChannelCount);
			for (unsigned i = 0; i < outputChannelCount; ++i) {
				outputBuf2D[i] = make_unique<double[]>(frameCount);
				outputBuf2DPtrs[i] = outputBuf2D[i].get();
			}
		}
		catch (const std::bad_alloc& e) {
			LogF(L"FATAL: Failed to allocate audio buffers. Exception: %S", e.what());
			allocatedFrameCount = 0;
		}
	}
}

void FilterEngine::setPreMix(bool preMix)
{
	this->preMix = preMix;
}

void FilterEngine::setDeviceInfo(bool capture, bool postMixInstalled, const wstring& deviceName, const wstring& connectionName, const wstring& deviceGuid, const wstring& deviceString)
{
	this->capture = capture;
	this->postMixInstalled = postMixInstalled;
	this->deviceName = deviceName;
	this->connectionName = connectionName;
	this->deviceGuid = deviceGuid;
	this->deviceString = deviceString;
}

void FilterEngine::initialize(float sampleRate, unsigned inputChannelCount, unsigned realChannelCount, unsigned outputChannelCount, unsigned channelMask, unsigned maxFrameCount, const wstring& customPath)
{
	bool shouldLoadConfig = false;

	{
		lock_guard<mutex> lock(loadMutex);

		cleanupConfigurations();

		this->sampleRate = sampleRate;
		this->inputChannelCount = inputChannelCount;
		this->realChannelCount = realChannelCount;
		this->outputChannelCount = outputChannelCount;
		this->maxFrameCount = maxFrameCount;
		this->transitionCounter = 0;
		this->transitionLength = (unsigned)(sampleRate / 100);
		resizeBuffers(maxFrameCount);

		unsigned deviceChannelCount;
		if (capture)
			deviceChannelCount = inputChannelCount;
		else
			deviceChannelCount = outputChannelCount;

		if (channelMask == 0)
			channelMask = ChannelHelper::getDefaultChannelMask(deviceChannelCount);

		this->channelMask = channelMask;

		vector<wstring> channelNames = ChannelHelper::getChannelNames(deviceChannelCount, channelMask);
		TraceF(L"%d channels for this device: %s", deviceChannelCount, StringHelper::join(channelNames, L" ").c_str());

		try
		{
			configPath = RegistryHelper::readValue(APP_REGPATH, L"ConfigPath");
		}
		catch (const RegistryException& e)
		{
			LogF(L"Can't read config path because of: %s", e.getMessage().c_str());
			return;
		}

		parser->ClearConst();
		parser->ClearFun();
		parser->ClearInfixOprt();
		parser->ClearOprt();
		parser->ClearPostfixOprt();
		parser->AddPackage(PackageCommon::Instance());
		parser->AddPackage(PackageNonCmplx::Instance());
		parser->AddPackage(PackageStr::Instance());
		parser->AddPackage(PackageMatrix::Instance());

		for (const auto& factory : factories)
			factory->initialize(this);

		shouldLoadConfig = configPath != L"";
	}

	if (shouldLoadConfig)
	{
		loadConfig(customPath);

		lock_guard<mutex> lock(loadMutex);
		if (!notificationWorker.joinable() && customPath.empty())
		{
			shutdownEvent = make_unique<Win32Event>(true, false);
			notificationWorker = thread(notificationThread, this);
			TraceF(L"Successfully created directory change notification thread for %s and its subtree", configPath.c_str());
		}
	}
}

void FilterEngine::loadConfig(const wstring& customPath)
{
	lock_guard<mutex> lock(loadMutex);
	timer.start();
	previousConfig.reset();

	allChannelNames = ChannelHelper::getChannelNames(max(realChannelCount, outputChannelCount), channelMask);

	currentChannelNames = allChannelNames;
	lastChannelNames.clear();
	lastNewChannelNames.clear();
	watchRegistryKeys.clear();
	parser->ClearVar();

	for (auto it = factories.cbegin(); it != factories.cend(); it++)
	{
		IFilterFactory* factory = it->get();
		vector<IFilter*> newFilters = factory->startOfConfiguration();
		if (!newFilters.empty())
			addFilters(newFilters);
	}

	if (customPath.empty())
		loadConfigFile(configPath + L"\\config.txt");
	else
		loadConfigFile(customPath);

	for (auto it = factories.cbegin(); it != factories.cend(); it++)
	{
		IFilterFactory* factory = it->get();
		vector<IFilter*> newFilters = factory->endOfConfiguration();
		if (!newFilters.empty())
			addFilters(newFilters);
	}

	void* mem = MemoryHelper::alloc(sizeof(FilterConfiguration));
	FilterConfigurationPtr config(new(mem) FilterConfiguration(this, move(filterInfos), (unsigned)allChannelNames.size()));

	filterInfos.clear();

	double loadTime = timer.stop();
	TraceF(L"Finished loading configuration after %lf milliseconds", loadTime * 1000.0);

	if (!currentConfig)
		currentConfig = move(config);
	else
		nextConfig = move(config);
}

void FilterEngine::loadConfigFile(const wstring& path)
{
	TraceF(L"Loading configuration from %s", path.c_str());

	stringstream inputStream = ConfigurationFileReader::readWithRetry(path);
	if (!inputStream.good())
		return;

	vector<wstring> savedChannelNames = currentChannelNames;

	for (auto it = factories.cbegin(); it != factories.cend(); it++)
	{
		IFilterFactory* factory = it->get();
		vector<IFilter*> newFilters = factory->startOfFile(path);
		if (!newFilters.empty())
			addFilters(newFilters);
	}

	while (inputStream.good())
	{
		string encodedLine;
		getline(inputStream, encodedLine);
		if (encodedLine.size() > 0 && encodedLine[encodedLine.size() - 1] == '\r')
			encodedLine.resize(encodedLine.size() - 1);

		wstring line = StringHelper::toWString(encodedLine, CP_UTF8);
		if (line.find(L'\uFFFD') != -1)
			line = StringHelper::toWString(encodedLine, CP_ACP);

		size_t pos = line.find(L':');
		if (pos != -1)
		{
			wstring key = line.substr(0, pos);
			wstring value = line.substr(pos + 1);

			// allow to use indentation
			key = StringHelper::trim(key);

			for (auto it = factories.cbegin(); it != factories.cend(); it++)
			{
				IFilterFactory* factory = it->get();

				vector<IFilter*> newFilters;
				try
				{
					newFilters = factory->createFilter(path, key, value);
				}
				catch (const exception& e)
				{
					LogF(L"%S", e.what());
				}

				if (key == L"")
					break;
				if (!newFilters.empty())
				{
					addFilters(newFilters);
					break;
				}
			}
		}
	}

	for (auto it = factories.cbegin(); it != factories.cend(); it++)
	{
		IFilterFactory* factory = it->get();
		vector<IFilter*> newFilters = factory->endOfFile(path);
		if (!newFilters.empty())
			addFilters(newFilters);
	}

	// restore channels selected in outer configuration file
	currentChannelNames = savedChannelNames;
}

void FilterEngine::watchRegistryKey(const std::wstring& key)
{
	watchRegistryKeys.insert(key);
}

#pragma AVRT_CODE_BEGIN
void convertFloatToDouble(double* dest, const float* src, size_t count) {
#if defined(__AVX512F__) && !defined(_M_ARM64) // AVX-512 Path (e.g., Zen 4, some Intel CPUs)
	size_t i = 0;
	for (; i + 16 <= count; i += 16) {
		// Load 16 floats
		__m512 float_vec = _mm512_loadu_ps(src + i);
		// Convert the lower 8 floats to 8 doubles
		__m512d double_vec_lo = _mm512_cvtps_pd(_mm512_extractf32x8_ps(float_vec, 0));
		// Convert the upper 8 floats to 8 doubles
		__m512d double_vec_hi = _mm512_cvtps_pd(_mm512_extractf32x8_ps(float_vec, 1));
		// Store the 16 resulting doubles
		_mm512_storeu_pd(dest + i, double_vec_lo);
		_mm512_storeu_pd(dest + i + 8, double_vec_hi);
	}
	// Handle any remaining elements
	for (; i < count; ++i) dest[i] = static_cast<double>(src[i]);
#elif defined(__AVX2__) && !defined(_M_ARM64) // AVX2 / AVX Fallback Path (e.g., Zen 2/3)
	size_t i = 0;
	for (; i + 8 <= count; i += 8) {
		// Load 8 floats into a 256-bit register
		__m256 float_vec = _mm256_loadu_ps(src + i);
		// Convert the lower 4 floats to 4 doubles
		__m256d double_vec_lo = _mm256_cvtps_pd(_mm256_extractf128_ps(float_vec, 0));
		// Convert the upper 4 floats to 4 doubles
		__m256d double_vec_hi = _mm256_cvtps_pd(_mm256_extractf128_ps(float_vec, 1));
		// Store the 8 resulting doubles
		_mm256_storeu_pd(dest + i, double_vec_lo);
		_mm256_storeu_pd(dest + i + 4, double_vec_hi);
	}
	// Handle any remaining elements
	for (; i < count; ++i) dest[i] = static_cast<double>(src[i]);
#else // Scalar fallback for non-x86 or very old CPUs
	for (size_t i = 0; i < count; ++i) dest[i] = static_cast<double>(src[i]);
#endif
}

// Converts a block of doubles back to floats.
void convertDoubleToFloat(float* dest, const double* src, size_t count) {
#if defined(__AVX512F__) && !defined(_M_ARM64) // AVX-512 Path
	size_t i = 0;
	for (; i + 16 <= count; i += 16) {
		// Load 16 doubles from memory
		__m512d double_vec_lo = _mm512_loadu_pd(src + i);
		__m512d double_vec_hi = _mm512_loadu_pd(src + i + 8);
		// Convert 8 doubles to 8 floats
		__m256 float_vec_lo = _mm512_cvtpd_ps(double_vec_lo);
		// Convert another 8 doubles to 8 floats
		__m256 float_vec_hi = _mm512_cvtpd_ps(double_vec_hi);
		// Combine the two 256-bit float vectors into one 512-bit vector
		__m512 float_vec = _mm512_insertf32x8(_mm512_castps256_ps512(float_vec_lo), float_vec_hi, 1);
		_mm512_storeu_ps(dest + i, float_vec);
	}
	for (; i < count; ++i) dest[i] = static_cast<float>(src[i]);
#elif defined(__AVX2__) && !defined(_M_ARM64) // AVX2 / AVX Fallback Path
	size_t i = 0;
	for (; i + 8 <= count; i += 8) {
		// Load 8 doubles from memory
		__m256d double_vec_lo = _mm256_loadu_pd(src + i);
		__m256d double_vec_hi = _mm256_loadu_pd(src + i + 4);
		// Convert 4 doubles to 4 floats
		__m128 float_vec_lo = _mm256_cvtpd_ps(double_vec_lo);
		// Convert another 4 doubles to 4 floats
		__m128 float_vec_hi = _mm256_cvtpd_ps(double_vec_hi);
		// Combine the two 128-bit float vectors into one 256-bit vector
		__m256 float_vec = _mm256_insertf128_ps(_mm256_castps128_ps256(float_vec_lo), float_vec_hi, 1);
		_mm256_storeu_ps(dest + i, float_vec);
	}
	for (; i < count; ++i) dest[i] = static_cast<float>(src[i]);
#else // Scalar fallback
	for (size_t i = 0; i < count; ++i) dest[i] = static_cast<float>(src[i]);
#endif
}


// Process interleaved audio (float*)
void FilterEngine::process(float* output, float* input, unsigned frameCount)
{
	if (currentConfig->isEmpty() && !nextConfig)
	{
		// Bypass mode: if no filters are active, just copy input to output if necessary.
		if (realChannelCount == outputChannelCount && input != output) {
			std::copy_n(input, outputChannelCount * frameCount, output);
		}
		return;
	}

	// Ensure our internal buffers are large enough
	resizeBuffers(frameCount);

	// Conversion from float to double using SIMD
	const unsigned inputSampleCount = inputChannelCount * frameCount;
	convertFloatToDouble(inputBuf1D.data(), input, inputSampleCount);

	// The core processing logic remains unchanged
	currentConfig->read(inputBuf1D.data(), frameCount);
	currentConfig->process(frameCount);

	if (nextConfig)
	{
		nextConfig->read(inputBuf1D.data(), frameCount);
		nextConfig->process(frameCount);
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength);
	}

	currentConfig->write(outputBuf1D.data(), frameCount);

	// Conversion from double back to float using SIMD
	const unsigned outputSampleCount = outputChannelCount * frameCount;
	convertDoubleToFloat(output, outputBuf1D.data(), outputSampleCount);

	finishTransitionIfReady();
}

// Process non-interleaved audio (float**)
void FilterEngine::process(float** output, float** input, unsigned frameCount)
{
	if (currentConfig->isEmpty() && !nextConfig)
	{
		// Bypass mode
		if (realChannelCount == outputChannelCount && input != output) {
			for (unsigned c = 0; c < realChannelCount; c++)
				std::copy_n(input[c], frameCount, output[c]);
		}
		return;
	}

	resizeBuffers(frameCount);

	// Optimized conversion for each channel
	for (unsigned c = 0; c < inputChannelCount; c++) {
		convertFloatToDouble(inputBuf2D[c].get(), input[c], frameCount);
	}

	// Core processing logic is the same
	currentConfig->read(inputBuf2DPtrs.data(), frameCount);
	currentConfig->process(frameCount);

	if (nextConfig)
	{
		nextConfig->read(inputBuf2DPtrs.data(), frameCount);
		nextConfig->process(frameCount);
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength);
	}

	currentConfig->write(outputBuf2DPtrs.data(), frameCount);

	// Optimized conversion back for each channel
	for (unsigned c = 0; c < outputChannelCount; c++) {
		convertDoubleToFloat(output[c], outputBuf2D[c].get(), frameCount);
	}

	// Transition logic remains the same
	finishTransitionIfReady();
}

// Process interleaved audio (double*) - native double precision without conversion
void FilterEngine::process(double* output, double* input, unsigned frameCount)
{
	if (currentConfig->isEmpty() && !nextConfig)
	{
		// Bypass mode: if no filters are active, just copy input to output if necessary.
		if (realChannelCount == outputChannelCount && input != output) {
			std::copy_n(input, outputChannelCount * frameCount, output);
		}
		return;
	}

	// Direct double-precision processing - no float conversion needed!
	currentConfig->read(input, frameCount);
	currentConfig->process(frameCount);

	if (nextConfig)
	{
		nextConfig->read(input, frameCount);
		nextConfig->process(frameCount);
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength);
	}

	currentConfig->write(output, frameCount);

	finishTransitionIfReady();
}

// Process non-interleaved audio (double**) - native double precision without conversion
void FilterEngine::process(double** output, double** input, unsigned frameCount)
{
	if (currentConfig->isEmpty() && !nextConfig)
	{
		// Bypass mode
		if (realChannelCount == outputChannelCount && input != output) {
			for (unsigned c = 0; c < realChannelCount; c++)
				std::copy_n(input[c], frameCount, output[c]);
		}
		return;
	}

	// Direct double-precision processing - no float conversion needed!
	currentConfig->read(input, frameCount);
	currentConfig->process(frameCount);

	if (nextConfig)
	{
		nextConfig->read(input, frameCount);
		nextConfig->process(frameCount);
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength);
	}

	currentConfig->write(output, frameCount);

	finishTransitionIfReady();
}
#pragma AVRT_CODE_END

void FilterEngine::addFilters(const vector<IFilter*>& filters)
{
	for (vector<IFilter*>::const_iterator it = filters.begin(); it != filters.end(); it++)
	{
		IFilter* filter = *it;
		auto filterInfo = make_unique<FilterInfo>();
		filterInfo->filter.reset(filter);
		filterInfo->inPlace = filter->getInPlace();
		vector<wstring> savedChannelNames = currentChannelNames;
		bool allChannels = filter->getAllChannels();
		if (allChannels)
			currentChannelNames = allChannelNames;

		if (lastChannelNames == currentChannelNames)
		{
			filterInfo->inChannels.clear();
		}
		else
		{
			filterInfo->inChannels.resize(currentChannelNames.size());

			size_t c = 0;
			for (vector<wstring>::iterator it2 = currentChannelNames.begin(); it2 != currentChannelNames.end(); it2++)
			{
				vector<wstring>::iterator pos = find(allChannelNames.begin(), allChannelNames.end(), *it2);
				filterInfo->inChannels[c++] = pos - allChannelNames.begin();
			}
		}

		lastChannelNames = currentChannelNames;

		vector<wstring> newChannelNames = filter->initialize(sampleRate, maxFrameCount, currentChannelNames);

		if (filterInfo->inPlace && lastInPlace && lastNewChannelNames == newChannelNames)
		{
			filterInfo->outChannels.clear();
		}
		else
		{
			filterInfo->outChannels.resize(newChannelNames.size());

			size_t c = 0;
			for (vector<wstring>::iterator it2 = newChannelNames.begin(); it2 != newChannelNames.end(); it2++)
			{
				vector<wstring>::iterator pos = find(allChannelNames.begin(), allChannelNames.end(), *it2);
				if (pos == allChannelNames.end())
				{
					filterInfo->outChannels[c++] = allChannelNames.size();
					allChannelNames.push_back(*it2);
				}
				else
				{
					filterInfo->outChannels[c++] = pos - allChannelNames.begin();
				}
			}
		}

		lastNewChannelNames = newChannelNames;
		lastInPlace = filterInfo->inPlace;
		if (!lastInPlace)
			swap(lastChannelNames, lastNewChannelNames);

		filterInfos.push_back(move(filterInfo));

		if (filter->getSelectChannels())
			currentChannelNames = newChannelNames;
		else
			currentChannelNames = savedChannelNames;
	}
}

void FilterEngine::cleanupConfigurations()
{
	currentConfig.reset();
	nextConfig.reset();
	previousConfig.reset();
}

bool FilterEngine::acquireLoadPermit()
{
	unique_lock<mutex> lock(loadPermitMutex);
	loadPermitCv.wait(lock, [&] {return shutdownRequested || loadPermitAvailable; });
	if (shutdownRequested)
		return false;

	loadPermitAvailable = false;
	return true;
}

void FilterEngine::releaseLoadPermit()
{
	{
		lock_guard<mutex> lock(loadPermitMutex);
		loadPermitAvailable = true;
	}
	loadPermitCv.notify_one();
}

void FilterEngine::finishTransitionIfReady()
{
	if (nextConfig && transitionCounter >= transitionLength)
	{
		previousConfig = move(currentConfig);
		currentConfig = move(nextConfig);
		transitionCounter = 0;
		releaseLoadPermit();
	}
}

void FilterEngine::notificationThread(FilterEngine* engine)
{

	HANDLE notificationHandle = FindFirstChangeNotificationW(engine->configPath.c_str(), true, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
	if (notificationHandle == INVALID_HANDLE_VALUE)
		notificationHandle = nullptr;

	Win32Event registryEvent(true, false);

	HANDLE handles[3] = {engine->shutdownEvent->get(), notificationHandle, registryEvent.get()};
	while (true)
	{
		vector<HKEY> keyHandles;
		for (auto it = engine->watchRegistryKeys.begin(); it != engine->watchRegistryKeys.end(); it++)
		{
			try
			{
				HKEY keyHandle = RegistryHelper::openKey(*it, KEY_NOTIFY | KEY_WOW64_64KEY);
				keyHandles.push_back(keyHandle);
				RegNotifyChangeKeyValue(keyHandle, false, REG_NOTIFY_CHANGE_LAST_SET, registryEvent.get(), true);
			}
			catch (const RegistryException& e)
			{
				LogFStatic(L"%s", e.getMessage().c_str());
			}
		}

		DWORD which = Win32Event::waitAny(3, handles);

		for (auto it = keyHandles.begin(); it != keyHandles.end(); it++)
		{
			RegCloseKey(*it);
		}

		if (which == WAIT_OBJECT_0)
		{
			// Shutdown
			break;
		}
		else
		{
			if (which == WAIT_OBJECT_0 + 1)
			{
				FindNextChangeNotification(notificationHandle);
				// Wait for second event within 10 milliseconds to avoid loading twice
				Win32Event::waitOne(notificationHandle, 10);
			}

			if (!engine->acquireLoadPermit())
			{
				// Shutdown
				break;
			}

			engine->loadConfig();
			FindNextChangeNotification(notificationHandle);
			registryEvent.reset();
		}
	}

	FindCloseChangeNotification(notificationHandle);
}
