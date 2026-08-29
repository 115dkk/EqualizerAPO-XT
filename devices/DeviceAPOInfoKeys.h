/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>

#include "services/registry/WindowsRegistry.h"
#include "services/registry/RegistryPaths.h"

// Registry vocabulary shared by DeviceAPOInfo's split implementation. Keeping
// install, load, state and uninstall on this table makes ownership symmetric.
#define protectedDGKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio"
#define protectedDGValueName L"DisableProtectedAudioDG"
#define apoRegistrationKeyPath L"HKEY_CLASSES_ROOT\\AudioEngine\\AudioProcessingObjects"
// Audit #250 F022: DllRegisterServer writes this tree as
// HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID; readers used to spell it
// HKEY_CLASSES_ROOT\CLSID. Same physical key on a real machine, but a fake
// registry cannot know that - one spelling everywhere.
#define clsidKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID"
#define commonKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio"
#define renderKeyPath commonKeyPath L"\\Render"
#define captureKeyPath commonKeyPath L"\\Capture"
#define childApoPath APP_REGPATH L"\\Child APOs"

// Audit #250 F020: vocabulary shared across binaries. The pipe name is the
// DeviceSelector <-> APO DLL test protocol (a divergent copy means the device
// test silently never answers); the service name is what every restart path
// asks the SCM for.
inline constexpr wchar_t deviceTestPipeValueName[] = L"DeviceTestPipeName";

// The APO only ever connects to \\.\pipe\<name>. A name carrying a path
// separator or a dot component could steer that CreateFileW out of the pipe
// namespace (\\.\pipe\..\X), so the reader rebuilds the name from this
// allow-list and treats anything else as "no test pipe" (CodeQL #9/#10).
// DeviceTestThread writes a constant that fits; keep the two in step.
inline std::wstring sanitizeDeviceTestPipeName(const std::wstring& value)
{
	std::wstring name;
	if (value.empty() || value.size() > 128)
		return name;
	name.reserve(value.size());
	for (size_t i = 0; i < value.size(); i++)
	{
		const wchar_t c = value[i];
		const bool allowed = (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z')
			|| (c >= L'a' && c <= L'z') || c == L'_' || c == L'-';
		if (!allowed)
			return std::wstring();
		name.push_back(c);
	}
	return name;
}

inline constexpr wchar_t audioServiceName[] = L"AudioSrv";

inline constexpr wchar_t preMixChildGuidValueName[] = L"PreMixChild";
inline constexpr wchar_t postMixChildGuidValueName[] = L"PostMixChild";
inline constexpr wchar_t allowSilentBufferValueName[] = L"AllowSilentBufferModification";
inline constexpr wchar_t disableAutoAdjustValueName[] = L"DisableAutomaticAdjustment";
inline constexpr wchar_t versionValueName[] = L"Version";
inline constexpr wchar_t connectionValueName[] = L"{a45c254e-df1c-4efd-8020-67d146a850e0},2";
inline constexpr wchar_t deviceValueName[] = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6";
inline constexpr wchar_t combinedDeviceValueName[] = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},41";
inline constexpr wchar_t formatValueName[] = L"{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0";
inline constexpr wchar_t channelMaskValueName[] = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},3";
inline constexpr wchar_t lfxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1";
inline constexpr wchar_t gfxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2";
inline constexpr wchar_t sfxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5";
inline constexpr wchar_t mfxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6";
inline constexpr wchar_t efxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";
inline constexpr wchar_t multiSfxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13";
inline constexpr wchar_t multiMfxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14";
inline constexpr wchar_t multiEfxGuidValueName[] = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15";
inline constexpr const wchar_t* allGuidValueNames[] = {
	lfxGuidValueName, gfxGuidValueName, sfxGuidValueName, mfxGuidValueName, efxGuidValueName
};
inline constexpr unsigned allGuidValueNameCount = static_cast<unsigned>(sizeof(allGuidValueNames) / sizeof(allGuidValueNames[0]));

enum GuidValueIndices
{
	LFX_INDEX = 0,
	GFX_INDEX = 1,
	SFX_INDEX = 2,
	MFX_INDEX = 3,
	EFX_INDEX = 4
};

inline constexpr wchar_t fxTitleValueName[] = L"{b725f130-47ef-101a-a5f1-02608c9eebac},10";
inline constexpr wchar_t sfxProcessingModesValueName[] = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},5";
inline constexpr wchar_t mfxProcessingModesValueName[] = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},6";
inline constexpr wchar_t efxProcessingModesValueName[] = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},7";
inline constexpr const wchar_t* ownedFxValueNames[] = {
	lfxGuidValueName, gfxGuidValueName, sfxGuidValueName, mfxGuidValueName, efxGuidValueName,
	sfxProcessingModesValueName, mfxProcessingModesValueName, efxProcessingModesValueName,
	fxTitleValueName
};
inline constexpr wchar_t defaultProcessingModeValue[] = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
// AUDIO_SIGNALPROCESSINGMODE_* (ksmedia.h) as the registry spells them. The
// audio engine only instantiates a stream or mode effect for the modes its
// PKEY_*_ProcessingModes_Supported_For_Streaming list names, and an app
// picks the mode through its stream category: voice chat runs in
// Communications, a personal assistant in Speech, music in Media, video in
// Movie, alerts in Notification. A list holding Default alone left every
// such stream without the EQ on drivers that declare those modes. Written
// only when the driver left no list of its own; Raw is never listed, raw
// streams bypass stream effects by design.
inline constexpr const wchar_t* captureProcessingModeValues[] = {
	L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}", // Default
	L"{98951333-B9CD-48B1-A0A3-FF40682D73F7}", // Communications
	L"{FC1CFC9B-B9D6-4CFA-B5E0-4BB2166878B2}"  // Speech
};
inline constexpr const wchar_t* renderProcessingModeValues[] = {
	L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}", // Default
	L"{4780004E-7133-41D8-8C74-660DADD2C0EE}", // Media
	L"{B26FEB0D-EC94-477C-9494-D1AB8E753F6E}", // Movie
	L"{98951333-B9CD-48B1-A0A3-FF40682D73F7}", // Communications
	L"{9CF2A70B-F377-403B-BD6B-360863E0355C}"  // Notification
};
inline constexpr wchar_t disableEnhancementsValueName[] = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";
inline constexpr wchar_t installVersion[] = L"2";
