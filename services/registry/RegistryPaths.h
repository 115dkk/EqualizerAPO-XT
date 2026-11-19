/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
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

// Persisted value names shared by writers and cleanup (audit #467 TD-47).
inline constexpr wchar_t installPathValueName[] = L"InstallPath";
inline constexpr wchar_t configPathValueName[] = L"ConfigPath";
inline constexpr wchar_t ignoreConvolutionSilenceValueName[] = L"IgnoreConvolutionSilence";
inline constexpr wchar_t parallelConvolutionValueName[] = L"ParallelConvolution";
inline constexpr wchar_t enableTraceValueName[] = L"EnableTrace";
inline constexpr wchar_t migratedFromValueName[] = L"MigratedFrom";
inline constexpr wchar_t migrationStampValueName[] = L"MigrationStamp";
inline constexpr wchar_t migratedFilesValueName[] = L"MigratedFiles";

#define protectedDGKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio"
inline constexpr wchar_t protectedDGValueName[] = L"DisableProtectedAudioDG";
#define apoRegistrationKeyPath L"HKEY_CLASSES_ROOT\\AudioEngine\\AudioProcessingObjects"
// Audit #250 F022: DllRegisterServer writes this tree as
// HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID; readers used to spell it
// HKEY_CLASSES_ROOT\CLSID. Same physical key on a real machine, but a fake
// registry cannot know that - one spelling everywhere.
#define clsidKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID"

// Audit #250 F020: the DeviceSelector <-> APO DLL test protocol. A divergent
// value name means the device test silently never answers.
inline constexpr wchar_t deviceTestPipeValueName[] = L"DeviceTestPipeName";
