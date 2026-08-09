/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// APO identities are registry vocabulary shared by the engine, installer,
// diagnostics, and tests. They do not belong to the live registry adapter.
inline constexpr GUID EQUALIZERAPO_PRE_MIX_GUID =
	{0xeacd2258, 0xfcac, 0x4ff4, {0xb3, 0x6d, 0x41, 0x9e, 0x92, 0x4a, 0x6d, 0x79}};
inline constexpr GUID EQUALIZERAPO_POST_MIX_GUID =
	{0xec1cc9ce, 0xfaed, 0x4822, {0x82, 0x8a, 0x82, 0xa8, 0x1a, 0x6f, 0x01, 0x8f}};

// These remain string-literal macros because several registry subpaths rely on
// compile-time literal concatenation.
#define APP_REGPATH L"HKEY_LOCAL_MACHINE\\SOFTWARE\\EqualizerAPO"
#define USER_REGPATH L"HKEY_CURRENT_USER\\SOFTWARE\\EqualizerAPO"
#define EDITOR_REGPATH USER_REGPATH L"\\Configuration Editor"
