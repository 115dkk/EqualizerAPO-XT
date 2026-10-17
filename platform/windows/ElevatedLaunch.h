/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <string>
#include "platform/windows/Win32Resource.h"
#include <shellapi.h>

namespace winutil
{
struct ElevatedLaunch
{
	bool started = false;
	DWORD error = ERROR_SUCCESS;
	UniqueHandle process;
};

inline ElevatedLaunch launchElevated(const std::wstring& executable, const std::wstring& arguments, bool keepProcess)
{
	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC | (keepProcess ? SEE_MASK_NOCLOSEPROCESS : 0);
	info.lpVerb = L"runas";
	info.lpFile = executable.c_str();
	info.lpParameters = arguments.c_str();
	info.nShow = SW_HIDE;
	ElevatedLaunch result;
	result.started = ShellExecuteExW(&info) != FALSE;
	result.error = result.started ? ERROR_SUCCESS : GetLastError();
	result.process.reset(info.hProcess);
	return result;
}
}
