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
	: factories(FilterFactoryRegistry::createFactories()),
	  preMix(false),
	  capture(false),
	  postMixInstalled(true),
	  inputChannelCount(0),
      realChannelCount(0),
      outputChannelCount(0),
	  lastInputWasSilent(false),
	  transitionCounter(0)
{
}

FilterEngine::~FilterEngine()
{
	// Make sure notification thread is terminated before cleaning up, otherwise deleted memory might be accessed in loadConfig
	if (notificationWorker.joinable())
	{
		configChannel.shutdown();
		notificationWorker.join();
		TraceF(L"Successfully terminated directory change notification thread");
	}

	cleanupConfigurations();
}

void FilterEngine::setPreMix(bool preMix)
{
	this->preMix = preMix;
}

bool FilterEngine::hasStatefulOrTailFilters() const
{
	if (configChannel.hasPending())
		return true;
	const FilterConfigurationPtr& currentConfig = configChannel.current();
	if (!currentConfig)
		return true;
	if (currentConfig->isEmpty())
		return false;
	return !currentConfig->isAllStateless();
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
		transitionFactorTable.resize(transitionLength);
		const double scale = M_PI / static_cast<double>(transitionLength);
		for (unsigned i = 0; i < transitionLength; i++)
			transitionFactorTable[i] = 0.5 * (1.0 - std::cos(i * scale));

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

		// The RT thread always starts from a valid empty configuration. Every
		// loaded file, including the initial one, is then published through the
		// same release/acquire handoff as a later reload.
		configChannel.reset(FilterConfigurationPtr(
			MemoryHelper::construct<FilterConfiguration>(
				streamFormat(), vector<std::unique_ptr<FilterInfo>>(), deviceChannelCount)));

		try
		{
			configPath = RegistryHelper::readValue(APP_REGPATH, L"ConfigPath");
		}
		catch (const RegistryException& e)
		{
			if (customPath.empty())
			{
				LogF(L"Can't read config path because of: %s", e.getMessage().c_str());
				return;
			}
			TraceF(L"Registry ConfigPath unavailable (%s); proceeding with caller-supplied custom path", e.getMessage().c_str());
		}

		parser.reinitialize();

		for (const auto& factory : factories)
			factory->initialize(this);

		shouldLoadConfig = !customPath.empty() || configPath != L"";
	}

	if (shouldLoadConfig)
	{
		const bool loaded = loadConfig(customPath);

		lock_guard<mutex> lock(loadMutex);
		// Initial/re-initialization loads still travel through ConfigSwapChannel,
		// but the first RT block must preserve the historical immediate-start
		// behavior rather than fading in from the seeded empty configuration.
		if (loaded)
			transitionCounter = transitionLength;
		if (!notificationWorker.joinable() && customPath.empty())
		{
			notificationWorker = thread(notificationThread, this);
			TraceF(L"Successfully created directory change notification thread for %s and its subtree", configPath.c_str());
		}
	}
}
