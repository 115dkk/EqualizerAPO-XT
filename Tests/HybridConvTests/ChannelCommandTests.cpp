/*
	This file is part of EqualizerAPO-XT.

	Round-trip tests for the shared "Channel:" config-line codec
	(filters/ChannelCommand.{h,cpp}), which the engine factory and the
	Editor GUI both consume.
*/

#include <string>
#include <vector>

#include "filters/ChannelCommand.h"
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
	harness.expectEqual(channels.size(), (size_t)2, "'L R' selector count");
	harness.expectTrue(channels[0] == L"L", "first selector");
	harness.expectTrue(channels[1] == L"R", "second selector");

	// Commas are separators too; the Editor GUI historically missed this.
	channels = parseChannels(L"L,R");
	harness.expectEqual(channels.size(), (size_t)2, "'L,R' selector count");
	harness.expectTrue(channels[0] == L"L", "'L,R' first selector");
	harness.expectTrue(channels[1] == L"R", "'L,R' second selector");

	// Mixed separators and position numbers.
	channels = parseChannels(L"1, c  SUB");
	harness.expectEqual(channels.size(), (size_t)3, "mixed separator selector count");
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
}
}

void runChannelCommandTests()
{
	testTokenization();
	testCommandRecognition();
	testRoundTrip();

	harness.report();
}
