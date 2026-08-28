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

		vector<winutil::UniqueRegistryKey> keyHandles;
		for (const wstring& key : snapshot.registryKeys)
		{
			try
			{
				keyHandles.push_back(WindowsRegistry::openKey(
					key, KEY_NOTIFY | KEY_WOW64_64KEY));
				RegNotifyChangeKeyValue(keyHandles.back().get(), false,
					REG_NOTIFY_CHANGE_LAST_SET, registryEvent.get(), true);
			}
			catch (const RegistryError& error)
			{
				LogFStatic(L"%s", error.getMessage().c_str());
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
		registryEvent.reset();
	}
}
