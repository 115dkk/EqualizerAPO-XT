/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class ConfigWatcher
{
public:
	struct Snapshot
	{
		std::wstring directory;
		std::vector<std::wstring> registryKeys;
	};

	using SnapshotProvider = std::function<Snapshot()>;
	using ChangeCallback = std::function<bool()>;

	ConfigWatcher(HANDLE shutdownEvent,
		SnapshotProvider snapshotProvider,
		ChangeCallback changeCallback);

	void run();

private:
	HANDLE shutdownEvent;
	SnapshotProvider snapshotProvider;
	ChangeCallback changeCallback;
};
