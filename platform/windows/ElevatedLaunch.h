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

enum class ElevatedLaunchMessage
{
	None,
	ApprovalDeclined,
	Failed
};

inline ElevatedLaunchMessage elevatedLaunchMessage(const ElevatedLaunch& launch)
{
	if (launch.started)
		return ElevatedLaunchMessage::None;
	return launch.error == ERROR_CANCELLED ? ElevatedLaunchMessage::ApprovalDeclined : ElevatedLaunchMessage::Failed;
}

inline ElevatedLaunch launchElevated(const std::wstring& executable, const std::wstring& arguments, bool keepProcess,
	int show = SW_HIDE)
{
	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC | (keepProcess ? SEE_MASK_NOCLOSEPROCESS : 0);
	info.lpVerb = L"runas";
	info.lpFile = executable.c_str();
	info.lpParameters = arguments.c_str();
	info.nShow = show;
	ElevatedLaunch result;
	result.started = ShellExecuteExW(&info) != FALSE;
	result.error = result.started ? ERROR_SUCCESS : GetLastError();
	result.process.reset(info.hProcess);
	return result;
}
}
