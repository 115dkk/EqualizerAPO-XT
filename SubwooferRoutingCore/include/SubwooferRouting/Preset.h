// SPDX-License-Identifier: MIT

#pragma once

#include "State.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace subroute
{

inline constexpr char kIssue246FrontRear41PresetId[] =
	"issue-246-front-rear-4.1";

struct PresetDescriptor
{
	std::string id;
	std::string displayName;
	std::string description;
};

struct PresetCreateResult
{
	std::optional<SubwooferRoutingState> state;
	std::string error;

	bool succeeded() const noexcept;
};

/*
	The returned vector has process-lifetime storage and must not be modified.
	Preset enumeration and creation are not real-time APIs.
*/
const std::vector<PresetDescriptor>& builtInPresets();

/*
	Creates a fresh, independent state for the requested stable preset ID.
	Unknown IDs return a result with no state and a non-empty error.
*/
PresetCreateResult createBuiltInPreset(std::string_view presetId);

/*
	The "Issue #246 - Front/Rear 4.1" preset is instantiated as follows:

	Layout, in order:
		L, R, LFE, RL, RR

	Speaker groups:
		Front:
			main paths FrontL and FrontR
			bass path FrontBass
		Rear:
			main paths RearL and RearR
			bass path RearBass

	Paths:
		FrontL:
			kind main
			source mix L at +1.0 linear
			high-pass 80 Hz, Q 0.707
			delay 2.5 ms

		FrontR:
			kind main
			source mix R at +1.0 linear
			high-pass 80 Hz, Q 0.707
			delay 2.5 ms

		FrontBass:
			kind bass
			source mix L at -1.0 linear and R at -1.0 linear
			two cascaded low-pass sections at 80 Hz, Q 0.707
			delay 0 ms

		RearL:
			kind main
			source mix RL at +1.0 linear
			high-pass 100 Hz, Q 0.707
			delay 2.0 ms

		RearR:
			kind main
			source mix RR at +1.0 linear
			high-pass 100 Hz, Q 0.707
			delay 2.0 ms

		RearBass:
			kind bass
			source mix RL at -1.0 linear and RR at -1.0 linear
			one low-pass section at 60 Hz, Q 0.707
			delay 0 ms

		SourceLFE:
			kind sourceLfe
			source mix LFE at +1.0 linear
			pre-gain +10 dB
			delay 0 ms

	Output matrix:
		L, replace:
			FrontL at 0 dB
			FrontBass at 0 dB
			SourceLFE at 0 dB

		R, replace:
			FrontR at 0 dB
			FrontBass at 0 dB
			SourceLFE at 0 dB

		LFE, replace:
			FrontBass at -14 dB
			SourceLFE at -14 dB
			RearBass at 0 dB

		RL, replace:
			RearL at 0 dB

		RR, replace:
			RearR at 0 dB

	Every path has preGainDb and postGainDb set to 0 dB except SourceLFE's
	+10 dB pre-gain. Every path chain contains one non-inverting
	PolarityStage, its crossover BiquadStage elements, one DelayStage, and one
	initially empty EqualizerSlotsStage. Headroom mode is Auto.
*/

}
