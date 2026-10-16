/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "platform/windows/Win32Resource.h"
#include <sddl.h>

namespace winutil::sids
{
inline constexpr wchar_t localService[] = L"S-1-5-19";
inline constexpr wchar_t users[] = L"S-1-5-32-545";
inline constexpr wchar_t administrators[] = L"S-1-5-32-544";
inline constexpr wchar_t system[] = L"S-1-5-18";

// ConvertStringSidToSid allocates with LocalAlloc, not AllocateAndInitializeSid.
inline UniqueLocalPtr<void> sidFromSddl(const wchar_t* text)
{
	UniqueLocalPtr<void> sid;
	if (!ConvertStringSidToSidW(text, sid.put()))
		return {};
	return sid;
}
}
