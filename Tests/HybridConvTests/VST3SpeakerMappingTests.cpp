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

	// Audit #348 TD-48: the proposals used to be written into a four-slot
	// array without a bound. The widest Auto list - two semantic, two
	// count-based and the plug-in's current arrangement - has five entries,
	// and every one of them must reach the plug-in in order.
	void testCandidatesBeyondFourSlots()
	{
		const vector<wstring> surround712 = {L"L", L"R", L"C", L"LFE", L"RL", L"RR",
			L"SL", L"SR", L"TSL", L"TSR"};
		expectArrangements(vst3speakers::arrangementCandidatesForLayout(
			VST3BusLayout::Auto, 8, surround712, SpeakerArr::k51),
			{SpeakerArr::k71_2, SpeakerArr::k71_2_TF, SpeakerArr::k71Music,
				SpeakerArr::k71Cine, SpeakerArr::k51},
			"five Auto candidates");
	}

	// Audit #348 TD-48: names, widths, parsing and the arrangement lists all
	// come from the two layout tables; this walks both, row by row.
	void testLayoutTable()
	{
		const struct
		{
			VST3BusLayout layout = VST3BusLayout::Auto;
			const wchar_t* name = L"";
			int channels = 0;
		} expected[] = {
			{VST3BusLayout::Auto, L"Auto", 0},
			{VST3BusLayout::Mono, L"Mono", 1},
			{VST3BusLayout::Stereo, L"Stereo", 2},
			{VST3BusLayout::Surround40, L"4.0", 4},
			{VST3BusLayout::Surround41, L"4.1", 5},
			{VST3BusLayout::Surround50, L"5.0", 5},
			{VST3BusLayout::Surround51, L"5.1", 6},
			{VST3BusLayout::Surround61, L"6.1", 7},
			{VST3BusLayout::Surround71, L"7.1", 8},
			{VST3BusLayout::Surround712, L"7.1.2", 10},
			{VST3BusLayout::Surround714, L"7.1.4", 12}
		};
		harness.expectEqual(std::size(vst3BusLayoutTable), std::size(expected), "one table row per layout");
		for (const auto& row : expected)
		{
			const string label = "layout " + std::to_string(static_cast<int>(row.layout));
			harness.expectTrue(wstring(vst3BusLayoutName(row.layout)) == row.name, label + ": config token");
			harness.expectEqual(vst3BusLayoutChannelCount(row.layout), row.channels, label + ": width");
			harness.expectEqual(vst3BusLayoutChannelNames(row.layout).size(), static_cast<size_t>(row.channels),
				label + ": one channel name per slot");
			VST3BusLayout parsed = VST3BusLayout::Auto;
			harness.expectTrue(parseVST3BusLayout(row.name, parsed) && parsed == row.layout,
				label + ": the token parses back");
			if (row.layout == VST3BusLayout::Auto)
				continue;

			const vector<SpeakerArrangement> candidates = vst3speakers::arrangementCandidatesForLayout(
				row.layout, 0, {}, SpeakerArr::kEmpty);
			harness.expectFalse(candidates.empty(), label + ": has speaker arrangements");
			for (SpeakerArrangement arrangement : candidates)
			{
				harness.expectEqual(SpeakerArr::getChannelCount(arrangement), row.channels,
					label + ": arrangement width matches the layout");
				harness.expectTrue(vst3speakers::layoutOfArrangement(arrangement) == row.layout,
					label + ": arrangement names the layout back");
			}
			// Mono's single C is not a semantic name set; every other layout's
			// own names propose its own arrangements.
			if (row.layout != VST3BusLayout::Mono)
			{
				expectArrangements(vst3speakers::semanticArrangementCandidates(
					vst3BusLayoutChannelNames(row.layout)), candidates, label + ": semantic names");
			}
		}
		VST3BusLayout parsed = VST3BusLayout::Stereo;
		harness.expectFalse(parseVST3BusLayout(L"7.1.3", parsed), "an unknown token is refused");
		harness.expectTrue(!vst3speakers::layoutOfArrangement(SpeakerArr::kEmpty).has_value(),
			"an empty arrangement names no layout");
		expectArrangements(vst3speakers::semanticArrangementCandidates({L"L", L"R", L"C", L"SL", L"SR"}),
			{SpeakerArr::k50}, "side spelling of 5.0");
		expectArrangements(vst3speakers::semanticArrangementCandidates({L"L", L"R", L"C", L"LFE", L"SL", L"SR"}),
			{}, "5.1 has no side spelling");
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
	testCandidatesBeyondFourSlots();
	testLayoutTable();
	testAutomaticNames();
	testMappings();
	testUnplaceableNames();
	harness.report();
}
