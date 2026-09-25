/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The device test's wire vocabulary: what the APO writes into the test pipe
	and what the Device Selector reads out of it (audit #348 F15).

	The keys and values used to be spelled twice, once in EqualizerAPO.cpp's
	sendMessage and once in DeviceTestThread, and a difference between the two
	shows up as a device test that never hears back, with nothing to say why.
	The registry value that tells the APO the pipe's name stays with the other
	registry names in DeviceAPOInfoKeys.h (deviceTestPipeValueName).

	One message per APO event, a JSON object:

		{"deviceGuid":"{...}", "stage":"PreMix", "phase":"Initialize"}

	stage is which of our two APOs is reporting, phase is how far it got:
	Initialize when the APO initialised on the endpoint, ChildAPO when the
	driver's own APO it chains to initialised as well.
*/

#pragma once

#include <string>

namespace devicetest::wire
{
// The pipe name the Device Selector publishes for the test. It has to pass
// sanitizeDeviceTestPipeName (DeviceAPOInfoKeys.h), which the APO applies.
inline constexpr wchar_t kPipeName[] = L"EqualizerAPODeviceTest";

inline constexpr char kKeyDeviceGuid[] = "deviceGuid";
inline constexpr char kKeyStage[] = "stage";
inline constexpr char kKeyPhase[] = "phase";

inline constexpr char kStagePreMix[] = "PreMix";
inline constexpr char kStagePostMix[] = "PostMix";

inline constexpr char kPhaseInitialize[] = "Initialize";
inline constexpr char kPhaseChildApo[] = "ChildAPO";

// Not an APO message: the Device Selector writes it into its own pipe to end
// the receiving thread (ReceiveThread::stop).
inline constexpr char kStopMessage[] = "stop";

// The message the APO sends. deviceGuidUtf8 is the endpoint GUID string as the
// endpoint property store gives it, in UTF-8; GUID strings need no escaping.
inline std::string composeMessage(const std::string& deviceGuidUtf8, bool preMix, const char* phase)
{
	return std::string("{\"") + kKeyDeviceGuid + "\":\"" + deviceGuidUtf8
		+ "\", \"" + kKeyStage + "\":\"" + (preMix ? kStagePreMix : kStagePostMix)
		+ "\", \"" + kKeyPhase + "\":\"" + phase + "\"}";
}
}
