/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <string>
#include <cstdio>
#include <windows.h>

namespace winutil
{
inline std::wstring localTimeStamp(bool fileName = false)
{
	SYSTEMTIME now;
	GetLocalTime(&now);
	wchar_t stamp[32];
	swprintf_s(stamp, fileName ? L"%04d%02d%02d-%02d%02d%02d" : L"%04d-%02d-%02d %02d:%02d:%02d",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	return stamp;
}
}
