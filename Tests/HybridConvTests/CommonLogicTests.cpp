/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Unit tests for the pure-logic WideString and ChannelLayout modules. These
	exercise only string manipulation and channel-name /
	channel-mask parsing, none of which needs a real audio device, registry
	access, or filter-engine setup. They link against the same Common.lib as
	HybridConvTests and run from its main() via runCommonLogicTests().
*/

#include <string>
#include "text/WideString.h"
#include "platform/windows/TextEncoding.h"
#include <vector>

#include "engine/IFilter.h"
#include "audio/ChannelLayout.h"
#include "Tests/TestHarness.h"

using std::wstring;
using std::vector;

namespace
{
test::Harness harness("CommonLogicTests");

class LifetimeProbeFilter : public IFilter
{
public:
	LifetimeProbeFilter(bool* destroyed)
		: destroyed(destroyed)
	{
	}

	~LifetimeProbeFilter() override
	{
		*destroyed = true;
	}

	std::vector<std::wstring> initialize(float, unsigned, std::vector<std::wstring> channelNames) override
	{
		return channelNames;
	}

	void process(double**, double**, unsigned) override
	{
	}

private:
	bool* destroyed;
};

void testOwningFilterPointer()
{
	bool destroyed = false;
	{
		FilterPtr filter = makeFilter<LifetimeProbeFilter>(&destroyed);
		harness.expectTrue(filter != nullptr, "makeFilter should return an owning filter pointer");
	}
	harness.expectTrue(destroyed, "FilterPtr should destroy and release the filter when ownership ends");
}

void testWideString()
{
	// trim removes leading/trailing whitespace, keeps interior, and collapses
	// an all-whitespace string to empty.
	harness.expectTrue(text::trim(L"  hello  ") == L"hello", "trim should strip surrounding spaces");
	harness.expectTrue(text::trim(L"\t a b \n") == L"a b", "trim keeps interior whitespace");
	harness.expectTrue(text::trim(L"   ") == L"", "all-whitespace trims to empty");
	harness.expectTrue(text::trim(L"x") == L"x", "single non-space is unchanged");

	// split on a separator, default skipEmpty == true.
	vector<wstring> words = text::split(L"a b  c", L' ');
	harness.requireEqual((int)words.size(), 3, "split should skip empty tokens by default");
	harness.expectTrue(words[0] == L"a" && words[1] == L"b" && words[2] == L"c", "split tokens mismatch");

	// split with skipEmpty == false keeps the empty token between the spaces.
	vector<wstring> withEmpty = text::split(L"a,,b", L',', false);
	harness.requireEqual((int)withEmpty.size(), 3, "split should keep empty tokens when asked");
	harness.expectTrue(withEmpty[1] == L"", "the middle empty token must be preserved");

	// join is the inverse of a simple split.
	harness.expectTrue(text::join({L"a", L"b", L"c"}, L" ") == L"a b c", "join with single-space separator");
	harness.expectTrue(text::join({}, L",") == L"", "join of nothing is empty");
	harness.expectTrue(text::join({L"only"}, L";") == L"only", "join of one element has no separator");
	harness.expectTrue(text::join({L"", L"b"}, L",") == L",b", "join preserves separators after empty values");

	// replaceCharacters swaps every character found in the set for the
	// replacement string.
	harness.expectTrue(
		text::replaceCharacters(L"a/b\\c", L"/\\", L"_") == L"a_b_c",
		"replaceCharacters should replace each listed char");
	harness.expectTrue(
		text::replaceIllegalFilenameCharacters(L"na:me?.txt") == L"na_me_.txt",
		"replaceIllegalCharacters should sanitise filename-illegal characters");

	// Case conversion (ASCII).
	harness.expectTrue(text::toUpper(L"Mixed123") == L"MIXED123", "toUpperCase");
	harness.expectTrue(text::toLower(L"Mixed123") == L"mixed123", "toLowerCase");

	// toWString / toString round trip through UTF-8 (CP_UTF8 == 65001; used as
	// a literal here so this translation unit does not need <windows.h>).
	const unsigned utf8CodePage = 65001u;
	wstring original = L"round-trip 123";
	std::string utf8 = wintext::toNarrowString(original, utf8CodePage);
	harness.expectTrue(wintext::toWideString(utf8, utf8CodePage) == original, "UTF-8 round trip should be lossless");

	// splitQuoted treats a quoted span as a single token even with separators
	// inside it.
	vector<wstring> quoted = text::splitQuoted(L"a \"b c\" d", L' ');
	harness.requireEqual((int)quoted.size(), 3, "splitQuoted token count");
	harness.expectTrue(quoted[1] == L"b c", "quoted span must survive splitQuoted as one token");
}

void testChannelLayout()
{
	// A stereo default mask must name exactly the front-left/right pair.
	int stereoMask = ChannelLayout::getDefaultChannelMask(2);
	vector<wstring> stereo = ChannelLayout::getChannelNames(2, stereoMask);
	harness.requireEqual((int)stereo.size(), 2, "stereo should have two channels");
	harness.expectTrue(stereo[0] == L"L" && stereo[1] == L"R", "stereo channel names should be L, R");

	// A 5.1 default mask must include the canonical surround names.
	int surroundMask = ChannelLayout::getDefaultChannelMask(6);
	vector<wstring> surround = ChannelLayout::getChannelNames(6, surroundMask);
	harness.requireEqual((int)surround.size(), 6, "5.1 should have six channels");
	harness.expectTrue(surround[0] == L"L" && surround[1] == L"R" && surround[2] == L"C" && surround[3] == L"LFE",
		"5.1 channel order should start L, R, C, LFE");

	// getChannelIndex by name, by 1-based number, and out-of-range / alias.
	harness.expectEqual(ChannelLayout::getChannelIndex(L"L", stereo), 0, "L resolves to index 0");
	harness.expectEqual(ChannelLayout::getChannelIndex(L"R", stereo), 1, "R resolves to index 1");
	harness.expectEqual(ChannelLayout::getChannelIndex(L"2", stereo), 1, "channel number 2 resolves to index 1");
	harness.expectEqual(ChannelLayout::getChannelIndex(L"5", stereo, true), -1, "out-of-range channel number is -1");
	// "SUB" is the legacy alias for the LFE channel.
	harness.expectEqual(ChannelLayout::getChannelIndex(L"SUB", surround), 3, "SUB alias resolves to the LFE index");

	// Audit #348 A2: the target rule Copy and MultiConvolution declare their
	// outputs by, and the Editor mirrors.
	const ChannelLayout::Target alias = ChannelLayout::resolveTarget(L"SUB", surround);
	harness.expectTrue(alias.name == L"LFE" && alias.index == 3, "a target alias names the existing channel");
	const ChannelLayout::Target number = ChannelLayout::resolveTarget(L"2", stereo);
	harness.expectTrue(number.name == L"R" && number.index == 1, "a target number names the existing channel");
	const ChannelLayout::Target fresh = ChannelLayout::resolveTarget(L"Wet", stereo);
	harness.expectTrue(fresh.name == L"Wet" && fresh.index == -1, "an unknown target is a new channel named as written");
	vector<wstring> declared = stereo;
	ChannelLayout::declare(declared, L"Wet");
	ChannelLayout::declare(declared, L"Wet");
	ChannelLayout::declare(declared, L"2");
	harness.expectTrue(declared == vector<wstring>({L"L", L"R", L"Wet"}),
		"declare appends a new channel once and leaves existing ones alone");

	// Audit #348 TD-20: the analysis layout rule the channel list and the
	// analysis thread share.
	const ChannelLayout::AnalysisLayout own = ChannelLayout::analysisLayout(6, surroundMask, 0);
	harness.expectTrue(own.channelCount == 6 && own.channelMask == 0,
		"no selection keeps the device's channel count");
	const ChannelLayout::AnalysisLayout same = ChannelLayout::analysisLayout(6, surroundMask, surroundMask);
	harness.expectTrue(same.channelCount == 6, "selecting the device's own mask keeps its channel count");
	const ChannelLayout::AnalysisLayout narrower = ChannelLayout::analysisLayout(6, surroundMask, stereoMask);
	harness.expectTrue(narrower.channelCount == 2 && narrower.channelMask == stereoMask,
		"another selection counts its mask bits");
	const ChannelLayout::AnalysisLayout unknown = ChannelLayout::analysisLayout(0, 0, 0);
	harness.expectTrue(unknown.channelCount == 8 && unknown.channelMask == ChannelLayout::getDefaultChannelMask(8),
		"a device with no channel count analyses as 7.1");
}
}

void runCommonLogicTests()
{
	testOwningFilterPointer();
	testWideString();
	testChannelLayout();
	harness.report();
}
