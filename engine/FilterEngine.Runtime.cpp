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

#include "helpers/RegistryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/ChannelHelper.h"
#include "ConfigurationFileReader.h"
#include "ConfigWatcher.h"
#include "FilterEngine.h"
// Filter factory headers intentionally omitted: the factories self-register and
// are pulled into the link via /WHOLEARCHIVE in the consumers; this TU names none
// of them (see FilterEngine.Configuration.cpp).

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


void FilterEngine::addFilters(FilterVector filters)
{
	for (FilterPtr& ownedFilter : filters)
	{
		auto filterInfo = make_unique<FilterInfo>();
		filterInfo->filter = move(ownedFilter);
		IFilter* filter = filterInfo->filter.get();
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
				if (pos == allChannelNames.end())
				{
					// Defensive: every currentChannelNames entry should already be in
					// allChannelNames (seeded from it, or a filter's own subset). If that
					// invariant is ever broken, append the name instead of storing a
					// one-past-the-end index that process() would read out of bounds; the
					// appended channel reads the zero-filled virtual range (silence).
					// Mirrors the outChannels handling below.
					filterInfo->inChannels[c++] = allChannelNames.size();
					allChannelNames.push_back(*it2);
				}
				else
				{
					filterInfo->inChannels[c++] = pos - allChannelNames.begin();
				}
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
	configChannel.reset();
}

bool FilterEngine::acquireLoadPermit()
{
	return configChannel.acquirePublishPermit();
}

void FilterEngine::releaseLoadPermit()
{
	configChannel.releasePublishPermit();
}

void FilterEngine::finishTransitionIfReady()
{
	// ConfigSwapChannel's acquire load observes the producer's fully-constructed
	// configuration before it is dereferenced on ARM64.
	if (configChannel.hasPending() && transitionCounter >= transitionLength)
	{
		configChannel.completeTransition();
		transitionCounter = 0;
	}
}

void FilterEngine::notificationThread(FilterEngine* engine)
{
	// Audit #250 F030: this is a std::thread body inside audiodg. An
	// exception escaping it (Win32Event construction, allocation in the
	// snapshot lambda) would reach std::terminate and take the audio
	// engine down with it. Watching stops, but the stream must survive.
	try
	{
		ConfigWatcher watcher(
			engine->configChannel.shutdownHandle(),
			[engine] {
				ConfigWatcher::Snapshot snapshot;
				lock_guard<mutex> lock(engine->loadMutex);
				snapshot.directory = engine->configPath;
				snapshot.registryKeys.assign(
					engine->watchRegistryKeys.begin(),
					engine->watchRegistryKeys.end());
				return snapshot;
			},
			[engine] {
				if (!engine->acquireLoadPermit())
					return false;

				const bool loaded = engine->loadConfig();
				if (!loaded)
					engine->releaseLoadPermit();
				return true;
			});
		watcher.run();
	}
	catch (const exception& e)
	{
		LogFStatic(L"Configuration watcher stopped by exception: %S", e.what());
	}
	catch (...)
	{
		LogFStatic(L"Configuration watcher stopped by an unknown exception");
	}
}
