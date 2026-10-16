/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "platform/windows/Win32Resource.h"
#include "platform/windows/Win32Error.h"
#include "runtime/errors/WideError.h"

namespace winutil
{
class PrivilegeScope
{
public:
	explicit PrivilegeScope(const wchar_t* name)
	{
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, token.put()))
			throw WideError(L"OpenProcessToken failed: " + win32::errorMessage(GetLastError()));
		TOKEN_PRIVILEGES enable = {};
		enable.PrivilegeCount = 1;
		if (!LookupPrivilegeValueW(nullptr, name, &enable.Privileges[0].Luid))
			throw WideError(L"LookupPrivilegeValue failed: " + win32::errorMessage(GetLastError()));
		enable.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		DWORD size = 0;
		if (!AdjustTokenPrivileges(token.get(), FALSE, &enable, sizeof(previous), &previous, &size))
			throw WideError(L"AdjustTokenPrivileges failed: " + win32::errorMessage(GetLastError()));
		// A successful call can report a privilege absent from this token.
		held = GetLastError() != ERROR_NOT_ALL_ASSIGNED;
	}

	~PrivilegeScope()
	{
		// Only changed privileges are returned, including when already enabled.
		if (previous.PrivilegeCount != 0)
			AdjustTokenPrivileges(token.get(), FALSE, &previous, 0, nullptr, nullptr);
	}

	PrivilegeScope(const PrivilegeScope&) = delete;
	PrivilegeScope& operator=(const PrivilegeScope&) = delete;
	bool isHeld() const {return held;}

private:
	UniqueHandle token;
	TOKEN_PRIVILEGES previous = {};
	bool held = false;
};
}
