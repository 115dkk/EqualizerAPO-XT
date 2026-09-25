/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <algorithm>
#include <string>
#include <vector>

#include "pluginterfaces/vst/vstspeaker.h"
#include "vst/VST3SpeakerMapping.h"
#include "Tests/TestHarness.h"

using namespace Steinberg::Vst;
using std::string;
using std::vector;
using std::wstring;

namespace
{
	test::Harness harness("VST3SpeakerMappingTests");

	void expectArrangements(const vector<SpeakerArrangement>& actual,
		const vector<SpeakerArrangement>& expected, const string& label)
	{
		harness.expectEqual(actual.size(), expected.size(), label + ": candidate count");
		const size_t count = (std::min)(actual.size(), expected.size());
		for (size_t i = 0; i < count; i++)
			harness.expectEqual(actual[i], expected[i], label + ": candidate order");
	}

	void expectMapping(SpeakerArrangement arrangement, const vector<wstring>& names,
		const vector<int>& expected, const string& label)
	{
		vector<int> mapping;
		harness.expectTrue(vst3speakers::buildChannelMapping(arrangement, names, mapping),
			label + ": mapping succeeds");
		harness.expectEqual(mapping.size(), expected.size(), label + ": slot count");
		const size_t count = (std::min)(mapping.size(), expected.size());
		for (size_t i = 0; i < count; i++)
			harness.expectEqual(mapping[i], expected[i], label + ": slot");
	}

	void testLayoutCandidates()
	{
		const struct
		{
			VST3BusLayout layout;
			vector<SpeakerArrangement> expected;
		} cases[] = {
			{VST3BusLayout::Auto, {SpeakerArr::k71Music, SpeakerArr::k71Cine}},
			{VST3BusLayout::Mono, {SpeakerArr::kMono}},
			{VST3BusLayout::Stereo, {SpeakerArr::kStereo}},
			{VST3BusLayout::Surround40, {SpeakerArr::k40Music, SpeakerArr::k40Cine}},
			{VST3BusLayout::Surround41, {SpeakerArr::k41Music, SpeakerArr::k41Cine}},
			{VST3BusLayout::Surround50, {SpeakerArr::k50}},
			{VST3BusLayout::Surround51, {SpeakerArr::k51}},
			{VST3BusLayout::Surround61, {SpeakerArr::k61Cine, SpeakerArr::k61Music}},
			{VST3BusLayout::Surround71, {SpeakerArr::k71Music, SpeakerArr::k71Cine}},
			{VST3BusLayout::Surround712, {SpeakerArr::k71_2, SpeakerArr::k71_2_TF}},
			{VST3BusLayout::Surround714, {SpeakerArr::k71_4}}
		};
		for (const auto& testCase : cases)
		{
			expectArrangements(vst3speakers::arrangementCandidatesForLayout(
				testCase.layout, 8, {}, SpeakerArr::kEmpty), testCase.expected,
				"layout " + std::to_string(static_cast<int>(testCase.layout)));
		}
	}

	void testChannelCountCandidates()
	{
		const struct
		{
			int channels = 0;
			vector<SpeakerArrangement> expected;
		} cases[] = {
			{1, {SpeakerArr::kMono}},
			{2, {SpeakerArr::kStereo}},
			{3, {}},
			{4, {SpeakerArr::k40Music, SpeakerArr::k40Cine}},
			{5, {SpeakerArr::k50}},
			{6, {SpeakerArr::k51}},
			{7, {SpeakerArr::k61Cine}},
			{8, {SpeakerArr::k71Music, SpeakerArr::k71Cine}},
			{10, {SpeakerArr::k71_2}},
			{12, {SpeakerArr::k71_4}}
		};
		for (const auto& testCase : cases)
		{
			expectArrangements(vst3speakers::arrangementCandidatesForChannelCount(
				testCase.channels, {}), testCase.expected,
				"channel count " + std::to_string(testCase.channels));
		}

		const vector<wstring> surround41 = {L"L", L"R", L"LFE", L"RL", L"RR"};
		expectArrangements(vst3speakers::arrangementCandidatesForChannelCount(5, surround41),
			{SpeakerArr::k41Music, SpeakerArr::k41Cine, SpeakerArr::k50},
			"semantic candidates precede count candidates");
	}

	void testAutomaticNames()
	{
		const vector<wstring> deviceNames = {L"L", L"R", L"LFE", L"RL", L"RR"};
		harness.expectTrue(vst3speakers::channelNamesForLayout(
			VST3BusLayout::Auto, deviceNames) == deviceNames,
			"Auto uses the device channel names");
		harness.expectTrue(vst3speakers::channelNamesForLayout(
			VST3BusLayout::Stereo, deviceNames) == vector<wstring>({L"L", L"R"}),
			"an explicit layout uses its own channel names");
	}

	void testMappings()
	{
		expectMapping(SpeakerArr::k51,
			{L"L", L"R", L"C", L"LFE", L"RL", L"RR"},
			{0, 1, 2, 3, 4, 5}, "5.1");
		expectMapping(SpeakerArr::k71Music,
			{L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"},
			{0, 1, 2, 3, 4, 5, 6, 7}, "7.1");
		expectMapping(SpeakerArr::k71_2,
			{L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR", L"TSL", L"TSR"},
			{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, "7.1.2 top-side");
		expectMapping(SpeakerArr::k71_4,
			{L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR",
				L"TFL", L"TFR", L"TRL", L"TRR"},
			{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, "7.1.4 top-front/rear");

		// With no distinct rear pair, EAPO's side names describe the VST3 Ls/Rs
		// roles. This keeps the Windows 4.0 side-only order placeable.
		expectMapping(SpeakerArr::k40Music, {L"L", L"R", L"SL", L"SR"},
			{0, 1, 2, 3}, "side-only channels use Ls/Rs");
	}

	void testUnplaceableNames()
	{
		vector<int> mapping;
		harness.expectFalse(vst3speakers::buildChannelMapping(SpeakerArr::k40Music,
			{L"L", L"R", L"Unknown", L"RR"}, mapping),
			"an unknown name cannot be placed");
		harness.expectTrue(mapping == vector<int>({0, 1, 2, 3}),
			"a failed mapping keeps identity order");

		harness.expectFalse(vst3speakers::buildChannelMapping(SpeakerArr::k71Cine,
			{L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"}, mapping),
			"names cannot claim speakers absent from the accepted arrangement");
	}
}

void runVST3SpeakerMappingTests()
{
	testLayoutCandidates();
	testChannelCountCandidates();
	testAutomaticNames();
	testMappings();
	testUnplaceableNames();
	harness.report();
}
