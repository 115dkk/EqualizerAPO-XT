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
	currentConfig.reset();
	nextConfig.reset();
	nextConfigReady.store(false, std::memory_order_relaxed);
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
	// Acquire: pairs with the release store in loadConfig so the dereference of
	// nextConfig below observes the fully-constructed configuration on ARM64.
	if (nextConfigReady.load(std::memory_order_acquire) && transitionCounter >= transitionLength)
	{
		previousConfig = move(currentConfig);
		currentConfig = move(nextConfig);
		// Cleared before releaseLoadPermit(); the permit mutex then publishes this
		// to the worker, which cannot store a new nextConfig until it reacquires.
		nextConfigReady.store(false, std::memory_order_relaxed);
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
		// watchRegistryKeys is cleared and refilled under loadMutex whenever a
		// configuration loads (loadConfig can run on the APO thread for a device
		// format change while this thread is between waits). Snapshot it under
		// the same mutex instead of iterating the live set.
		vector<wstring> watchedKeys;
		{
			lock_guard<mutex> lock(engine->loadMutex);
			watchedKeys.assign(engine->watchRegistryKeys.begin(), engine->watchRegistryKeys.end());
		}

		vector<HKEY> keyHandles;
		for (auto it = watchedKeys.begin(); it != watchedKeys.end(); it++)
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

			const bool loaded = engine->loadConfig();
			// A successful reload normally keeps the permit until the RT thread
			// finishes the crossfade. Failed loads publish nothing, and a recovery
			// load after an initially missing configuration installs currentConfig
			// directly; both cases must return the permit here.
			if (!loaded || !engine->nextConfigReady.load(std::memory_order_acquire))
				engine->releaseLoadPermit();
			FindNextChangeNotification(notificationHandle);
			registryEvent.reset();
		}
	}

	FindCloseChangeNotification(notificationHandle);
}
