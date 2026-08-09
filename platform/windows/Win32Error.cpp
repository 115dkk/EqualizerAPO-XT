/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
*/

#include "stdafx.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/Win32Error.h"
#include "platform/windows/Win32Resource.h"

std::wstring win32::errorMessage(long status)
{
	winutil::UniqueLocalPtr<wchar_t> buffer;
	if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, status, 0, reinterpret_cast<LPWSTR>(buffer.put()), 0, nullptr) == 0 || !buffer)
		return L"";
	std::wstring result(buffer.get());
	while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r'))
		result.pop_back();
	return result;
}
