/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#include "stdafx.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/Win32Error.h"
#include "platform/windows/Win32Resource.h"

std::wstring win32::errorMessage(long status)
{
	winutil::UniqueLocalPtr<wchar_t> buffer;
	const DWORD code = static_cast<DWORD>(status);
	const bool formatted = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, 0, reinterpret_cast<LPWSTR>(buffer.put()), 0, nullptr) != 0 && buffer;
	std::wstring result = formatted ? buffer.get() : L"Unknown Win32 error";
	while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r'))
		result.pop_back();
	return result + L" (error " + std::to_wstring(code) + L")";
}
