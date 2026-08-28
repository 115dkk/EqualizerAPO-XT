/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared "Channel:" config-line codec
	(filters/ChannelCommand.{h,cpp}), which the engine factory and the
	Editor GUI both consume.
*/

#include <string>
#include <vector>

#include "filters/ChannelCommand.h"
#include "filters/ChannelFilter.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("ChannelCommandTests");

std::vector<wstring> parseChannels(const wstring& parameters)
{
	ChannelCommand cmd;
	if (!ChannelCommand::parse(L"Channel", parameters, cmd))
		harness.fail("'Channel' command was not recognized");
	return cmd.channels;
}

void testTokenization()
{
	std::vector<wstring> channels = parseChannels(L" L R ");
	harness.requireEqual(channels.size(), (size_t)2, "'L R' selector count");
	harness.expectTrue(channels[0] == L"L", "first selector");
	harness.expectTrue(channels[1] == L"R", "second selector");

	// Commas are separators too; the Editor GUI historically missed this.
	channels = parseChannels(L"L,R");
	harness.requireEqual(channels.size(), (size_t)2, "'L,R' selector count");
	harness.expectTrue(channels[0] == L"L", "'L,R' first selector");
	harness.expectTrue(channels[1] == L"R", "'L,R' second selector");

	// Mixed separators and position numbers.
	channels = parseChannels(L"1, c  SUB");
	harness.requireEqual(channels.size(), (size_t)3, "mixed separator selector count");
	harness.expectTrue(channels[0] == L"1", "numeric selector");
	harness.expectTrue(channels[1] == L"C", "lower-case selector is upper-cased");
	harness.expectTrue(channels[2] == L"SUB", "name selector");

	// An empty selector list is a valid Channel line.
	channels = parseChannels(L"   ");
	harness.expectEqual(channels.size(), (size_t)0, "blank parameters give no selectors");
}

void testCommandRecognition()
{
	ChannelCommand cmd;
	harness.expectFalse(ChannelCommand::parse(L"Preamp", L"L R", cmd), "'Preamp' must not parse as Channel");
	harness.expectFalse(ChannelCommand::parse(L"channel", L"L R", cmd), "command match is case-sensitive");
}

void testRoundTrip()
{
	const wstring cases[] = {
		L"L R",
		L"L,R",
		L"  l   r,c ",
		L"1 2 3",
		L"",
	};

	for (const wstring& parameters : cases)
	{
		ChannelCommand first;
		ChannelCommand::parse(L"Channel", parameters, first);
		wstring serialized = first.serialize();

		ChannelCommand second;
		ChannelCommand::parse(L"Channel", serialized, second);
		harness.expectTrue(first.channels == second.channels, "serialize/parse round trip is stable");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}

	// Canonical form: single-space separated, upper-cased. This is the exact
	// text the Editor's in-place channel editor writes back, so it is pinned
	// here as the serialization-identity contract.
	ChannelCommand canonical;
	ChannelCommand::parse(L"Channel", L" c,l  r ", canonical);
	harness.expectTrue(canonical.serialize() == L"C L R", "canonical serialization is single-spaced and upper-cased");
}

void testResolveSelection()
{
	// resolveSelection must stay equivalent to ChannelFilter::initialize's
	// selection: a subset of channelNames in channelNames order, ALL
	// selecting everything, unknown selectors ignored. The equivalence run
	// below pins it to the real filter.
	const std::vector<wstring> names = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};

	std::vector<wstring> picked = ChannelCommand::resolveSelection({L"RR", L"L"}, names);
	harness.requireEqual(picked.size(), (size_t)2, "two selectors resolve to two channels");
	harness.expectTrue(picked[0] == L"L" && picked[1] == L"RR",
		"the selection keeps channel order, not written order");

	picked = ChannelCommand::resolveSelection({L"ALL"}, names);
	harness.expectTrue(picked == names, "ALL selects every channel");

	picked = ChannelCommand::resolveSelection({L"2", L"SL"}, names);
	harness.requireEqual(picked.size(), (size_t)2, "numbers and aliases resolve");
	harness.expectTrue(picked[0] == L"R" && picked[1] == L"RL",
		"position 2 is R and the SL alias lands on RL");

	picked = ChannelCommand::resolveSelection({L"NOSUCH", L"9"}, names);
	harness.expectTrue(picked.empty(), "unknown selectors are ignored");

	picked = ChannelCommand::resolveSelection({}, names);
	harness.expectTrue(picked.empty(), "an empty Channel line selects nothing");

	// Equivalence against the real filter over a few representative lines.
	const std::vector<std::vector<wstring>> tokenSets = {
		{L"L", L"R"}, {L"ALL"}, {L"SL", L"SR", L"1"}, {L"C", L"C", L"NOSUCH"}, {}};
	for (const std::vector<wstring>& tokens : tokenSets)
	{
		ChannelFilter filter(tokens);
		const std::vector<wstring> fromFilter = filter.initialize(48000.0f, 512, names);
		harness.expectTrue(ChannelCommand::resolveSelection(tokens, names) == fromFilter,
			"resolveSelection matches ChannelFilter::initialize");
	}
}
}

void runChannelCommandTests()
{
	testTokenization();
	testCommandRecognition();
	testRoundTrip();
	testResolveSelection();

	harness.report();
}
