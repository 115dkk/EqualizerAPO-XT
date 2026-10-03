/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

// Single owner of the "NewChannel:" config-line grammar, shared by the engine
// factory and the Editor's card and channel flow.
//
//   NewChannel: <name> [<name> ...]
//
// The line declares silent virtual channels and adds them to the current
// selection, so a VSTPlugin's OutputChannels or a Channel: line below it can
// name them. "Copy: VC=0" declares the same channel, but it does not select
// it, and "Copy: VC=1" copies channel 1 instead.
//
// Names are separated by whitespace or commas and upper-cased, because
// Channel: upper-cases its selectors and could never select a lower-case
// name. A name written twice on one line is kept once.
struct NewChannelCommand
{
	// Upper-cased names in the order they were written, duplicates removed.
	std::vector<std::wstring> names;

	// Canonical parameter string: the names joined by a single space.
	std::wstring serialize() const;

	// Returns true when command names a NewChannel line; names is then
	// filled from parameters. An empty list is the caller's to reject.
	static bool parse(const std::wstring& command, const std::wstring& parameters, NewChannelCommand& out);

	enum class Problem
	{
		None,
		// The engine would read it as a channel number.
		StartsWithDigit,
		// ALL selects every channel on a Channel: line.
		ReservedAll,
		// '=', '*', '+', '-', '.' or '`': Copy's grammar would split the
		// name, read it as a constant or as an inline expression.
		SeparatorCharacter,
		// A channel of the device, by name or by an alias the engine
		// accepts for one (SL/RL, SR/RR, SUB for LFE).
		DeviceChannel
	};

	// Judges one upper-cased name against the device's channel names.
	static Problem judgeName(const std::wstring& name, const std::vector<std::wstring>& deviceChannels);

	// Short English reason for a problem ("starts with a digit"); empty for
	// None. The factory's parse error and the Editor's warning both use it.
	static std::wstring describe(Problem problem);

	// Empty when every name is acceptable. Otherwise the reason the whole
	// line is rejected, naming each bad name with its problem, as in
	// "invalid channel name(s): 1X (starts with a digit), L (is a device channel)".
	static std::wstring validate(const std::vector<std::wstring>& names, const std::vector<std::wstring>& deviceChannels);

	// The selection a NewChannel line hands the lines below it: the
	// selection it received, then each name that is not already in it.
	static std::vector<std::wstring> extendSelection(std::vector<std::wstring> selection,
		const std::vector<std::wstring>& names);
};
