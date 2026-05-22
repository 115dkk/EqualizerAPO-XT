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
#include "filters/FilterFactoryRegistry.h"

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

	factories = FilterFactoryRegistry::createFactories();
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
