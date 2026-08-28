/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip and matching tests for the shared "Device:" config-line codec
	(filters/DeviceCommand.{h,cpp}), which the engine factory and the
	Editor GUI both consume.
*/

#include <string>
#include <vector>

#include "filters/DeviceCommand.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("DeviceCommandTests");

const wstring speakers = L"Speakers Realtek High Definition Audio {f47ee3f7-1111-2222-3333-25c0ddd38222}";
const wstring headphones = L"Headphones USB Audio Device {0a1b2c3d-4444-5555-6666-25c0ddd38222}";

DeviceCommand parsePatterns(const wstring& parameters)
{
	DeviceCommand cmd;
	if (!DeviceCommand::parse(L"Device", parameters, cmd))
		harness.fail("'Device' command was not recognized");
	return cmd;
}

void testTokenization()
{
	DeviceCommand cmd = parsePatterns(L"Speakers Realtek; USB Audio");
	harness.requireEqual(cmd.patterns.size(), (size_t)2, "two patterns are split on ';'");
	harness.requireEqual(cmd.patterns[0].size(), (size_t)2, "first pattern word count");
	harness.requireEqual(cmd.patterns[1].size(), (size_t)2, "second pattern word count");
	harness.expectTrue(cmd.patterns[0][0] == L"Speakers", "first word kept verbatim");
	harness.expectTrue(cmd.patterns[1][1] == L"Audio", "second pattern second word");

	// Empty words and empty patterns are dropped.
	cmd = parsePatterns(L"  Speakers ;; ; Realtek  ");
	harness.expectEqual(cmd.patterns.size(), (size_t)2, "empty patterns are dropped");

	cmd = parsePatterns(L"   ");
	harness.expectEqual(cmd.patterns.size(), (size_t)0, "blank parameters give no patterns");
}

void testCommandRecognition()
{
	DeviceCommand cmd;
	harness.expectFalse(DeviceCommand::parse(L"Preamp", L"all", cmd), "'Preamp' must not parse as Device");
	harness.expectFalse(DeviceCommand::parse(L"device", L"all", cmd), "command match is case-sensitive");
}

void testMatching()
{
	// "all" matches every device.
	harness.expectTrue(parsePatterns(L"all").matches(speakers), "'all' matches speakers");
	harness.expectTrue(parsePatterns(L"ALL").matches(headphones), "'all' is case-insensitive");

	// Every word of a pattern must occur (case-insensitive substring).
	harness.expectTrue(parsePatterns(L"speakers realtek").matches(speakers), "all words match");
	harness.expectFalse(parsePatterns(L"speakers usb").matches(speakers), "one missing word fails the pattern");

	// Any one pattern is enough.
	harness.expectTrue(parsePatterns(L"speakers usb; headphones").matches(headphones), "second pattern matches");

	// Words without '{' are matched with GUIDs removed from the device string;
	// words with '{' are matched against the full device string.
	harness.expectFalse(parsePatterns(L"f47ee3f7").matches(speakers), "plain word cannot hit a GUID fragment");
	harness.expectTrue(parsePatterns(L"{f47ee3f7").matches(speakers), "braced word matches into the GUID");

	// An empty pattern list matches nothing.
	harness.expectFalse(parsePatterns(L"").matches(speakers), "empty pattern list matches nothing");
}

void testRoundTrip()
{
	const wstring cases[] = {
		L"all",
		L"Speakers Realtek; USB Audio",
		L"  Speakers ;; Realtek  ",
		L"{f47ee3f7-1111-2222-3333-25c0ddd38222}",
		L"",
	};

	for (const wstring& parameters : cases)
	{
		DeviceCommand first;
		DeviceCommand::parse(L"Device", parameters, first);
		wstring serialized = first.serialize();

		DeviceCommand second;
		DeviceCommand::parse(L"Device", serialized, second);
		harness.expectTrue(first.patterns == second.patterns, "serialize/parse round trip is stable");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}
}

void runDeviceCommandTests()
{
	testTokenization();
	testCommandRecognition();
	testMatching();
	testRoundTrip();

	harness.report();
}
