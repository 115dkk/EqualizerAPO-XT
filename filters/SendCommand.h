/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "filters/CopyFilter.h"

struct SendCommand
{
	enum class LatencyUnit
	{
		Default,
		Milliseconds,
		Samples
	};

	enum class Mode
	{
		Mix,
		Replace
	};

	// The target endpoint GUID in canonical form: braces, lower case,
	// as Windows writes PKEY_AudioEndpoint_GUID ("{a6974eef-cbb1-...}").
	std::wstring endpoint;
	// Target = a channel name of the RECEIVING endpoint (not validated here);
	// summands = channels of this engine, each with an optional factor
	// (Copy's grammar, dB included). Constants are rejected.
	std::vector<Assignment> assignments;
	LatencyUnit latencyUnit = LatencyUnit::Default;
	double latency = 0.0;
	bool compensate = true;
	Mode mode = Mode::Mix;
	static constexpr size_t maxChannels = 16;
	static constexpr size_t maxTargetNameLength = 31;

	std::wstring serialize() const;
	// Returns false when command is not "Send" (exact, case-sensitive) or the
	// parameters are malformed; error (if given) then names what is wrong.
	// A false return with an empty error means "not a Send line".
	static bool parse(const std::wstring& command, const std::wstring& parameters,
		SendCommand& out, std::wstring* error = nullptr);
	// Canonical form of an endpoint GUID string, or empty if it is not one.
	static std::wstring canonicalEndpoint(const std::wstring& text);
};
