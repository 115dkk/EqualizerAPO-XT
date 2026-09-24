/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "platform/windows/Win32Error.h"

#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"
#include "platform/windows/Win32Event.h"
#include "platform/windows/Win32Resource.h"
#include "ConfigWatcher.h"

using std::wstring;
using std::vector;

namespace
{
struct WatchedKey
{
	wstring path;
	winutil::UniqueRegistryKey handle;
	bool failureLogged = false;
};

bool sameKeyList(const vector<WatchedKey>& watched, const vector<wstring>& keys)
{
	if (watched.size() != keys.size())
		return false;
	for (size_t i = 0; i < keys.size(); i++)
		if (watched[i].path != keys[i])
			return false;
	return true;
}

// Arming resets the event (it is an asynchronous registry I/O), which is why
// re-arming after a close never left it signalled. A key that was deleted
// cannot be armed again; dropping its handle makes the next pass reopen it.
void arm(WatchedKey& watched, HANDLE event)
{
	if (RegNotifyChangeKeyValue(watched.handle.get(), false,
		REG_NOTIFY_CHANGE_LAST_SET, event, true) != ERROR_SUCCESS)
		watched.handle = {};
}
}

ConfigWatcher::ConfigWatcher(HANDLE shutdownEvent,
	SnapshotProvider snapshotProvider,
	ChangeCallback changeCallback)
	: shutdownEvent(shutdownEvent),
	snapshotProvider(std::move(snapshotProvider)),
	changeCallback(std::move(changeCallback))
{
}

void ConfigWatcher::run()
{
	winutil::UniqueChangeNotification directoryNotification;
	wstring watchedDirectory;
	bool waitFailureLogged = false;
	bool watchFailureLogged = false;
	Win32Event registryEvent(true, false);
	vector<WatchedKey> watchedKeys;

	while (true)
	{
		const Snapshot snapshot = snapshotProvider();
		if (snapshot.directory != watchedDirectory)
		{
			directoryNotification.reset();
			watchedDirectory = snapshot.directory;
			watchFailureLogged = false;
		}

		if (!directoryNotification && !watchedDirectory.empty())
		{
			directoryNotification.reset(FindFirstChangeNotificationW(
				watchedDirectory.c_str(), true,
				FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE));
			if (!directoryNotification && !watchFailureLogged)
			{
				LogFStatic(L"Could not watch config directory %s; retrying with backoff: %s",
					watchedDirectory.c_str(),
					win32::errorMessage(GetLastError()).c_str());
				watchFailureLogged = true;
			}
		}

		// The watched keys stay open across passes instead of being reopened
		// on every 1 s backoff tick, and a key that cannot be opened is logged
		// once per key list rather than once per second. Closing an armed key
		// signals the event, so a key list change resets it after the close.
		if (!sameKeyList(watchedKeys, snapshot.registryKeys))
		{
			watchedKeys.clear();
			registryEvent.reset();
			for (const wstring& key : snapshot.registryKeys)
				watchedKeys.push_back(WatchedKey{key, {}, false});
		}
		// A key that does not exist yet (or was deleted) is retried on every
		// pass; opening a new key does not signal the event.
		for (WatchedKey& watched : watchedKeys)
		{
			if (watched.handle)
				continue;
			try
			{
				watched.handle = WindowsRegistry::openKey(
					watched.path, KEY_NOTIFY | KEY_WOW64_64KEY);
				arm(watched, registryEvent.get());
			}
			catch (const RegistryError& error)
			{
				if (!watched.failureLogged)
					LogFStatic(L"%s", error.getMessage().c_str());
				watched.failureLogged = true;
			}
		}

		HANDLE handles[3] = {shutdownEvent, registryEvent.get(), nullptr};
		DWORD handleCount = 2;
		DWORD directoryIndex = MAXDWORD;
		if (directoryNotification)
		{
			directoryIndex = handleCount;
			handles[handleCount++] = directoryNotification.get();
		}

		const DWORD waitResult = WaitForMultipleObjects(
			handleCount, handles, false,
			1000);
		if (waitResult == WAIT_OBJECT_0)
			break;
		if (waitResult == WAIT_TIMEOUT)
			continue;
		if (waitResult == WAIT_FAILED)
		{
			if (!waitFailureLogged)
			{
				LogFStatic(L"Config watcher wait failed; retrying with backoff: %s",
					win32::errorMessage(GetLastError()).c_str());
				waitFailureLogged = true;
			}
			if (WaitForSingleObject(shutdownEvent, 1000) == WAIT_OBJECT_0)
				break;
			continue;
		}
		waitFailureLogged = false;

		if (waitResult == WAIT_OBJECT_0 + 1)
		{
			// The asynchronous notification is one-shot: reset the event, then
			// arm every held key again before the reload reads the values.
			registryEvent.reset();
			for (WatchedKey& watched : watchedKeys)
				if (watched.handle)
					arm(watched, registryEvent.get());
		}

		const bool directoryChanged =
			directoryIndex != MAXDWORD
			&& waitResult == WAIT_OBJECT_0 + directoryIndex;
		if (directoryChanged)
		{
			if (!FindNextChangeNotification(directoryNotification.get()))
			{
				LogFStatic(L"Config directory watch could not be re-armed: %s",
					win32::errorMessage(GetLastError()).c_str());
				directoryNotification.reset();
			}
			else if (WaitForSingleObject(directoryNotification.get(), 10) == WAIT_OBJECT_0
				&& !FindNextChangeNotification(directoryNotification.get()))
			{
				LogFStatic(L"Config directory watch failed during debounce: %s",
					win32::errorMessage(GetLastError()).c_str());
				directoryNotification.reset();
			}
		}

		if (!changeCallback())
			break;
	}
}
