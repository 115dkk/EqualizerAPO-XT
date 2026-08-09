/*
	This file is part of EqualizerAPO-XT.

	Unit tests for the pure-logic Common helpers (StringHelper and
	ChannelHelper). These exercise only string manipulation and channel-name /
	channel-mask parsing, none of which needs a real audio device, registry
	access, or filter-engine setup. They link against the same Common.lib as
	HybridConvTests and run from its main() via runCommonLogicTests().
*/

#include <string>
#include <vector>

#include "engine/IFilter.h"
#include "text/StringHelper.h"
#include "audio/ChannelHelper.h"
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

void testStringHelper()
{
	// trim removes leading/trailing whitespace, keeps interior, and collapses
	// an all-whitespace string to empty.
	harness.expectTrue(StringHelper::trim(L"  hello  ") == L"hello", "trim should strip surrounding spaces");
	harness.expectTrue(StringHelper::trim(L"\t a b \n") == L"a b", "trim keeps interior whitespace");
	harness.expectTrue(StringHelper::trim(L"   ") == L"", "all-whitespace trims to empty");
	harness.expectTrue(StringHelper::trim(L"x") == L"x", "single non-space is unchanged");

	// split on a separator, default skipEmpty == true.
	vector<wstring> words = StringHelper::split(L"a b  c", L' ');
	harness.requireEqual((int)words.size(), 3, "split should skip empty tokens by default");
	harness.expectTrue(words[0] == L"a" && words[1] == L"b" && words[2] == L"c", "split tokens mismatch");

	// split with skipEmpty == false keeps the empty token between the spaces.
	vector<wstring> withEmpty = StringHelper::split(L"a,,b", L',', false);
	harness.requireEqual((int)withEmpty.size(), 3, "split should keep empty tokens when asked");
	harness.expectTrue(withEmpty[1] == L"", "the middle empty token must be preserved");

	// join is the inverse of a simple split.
	harness.expectTrue(StringHelper::join({L"a", L"b", L"c"}, L" ") == L"a b c", "join with single-space separator");
	harness.expectTrue(StringHelper::join({}, L",") == L"", "join of nothing is empty");
	harness.expectTrue(StringHelper::join({L"only"}, L";") == L"only", "join of one element has no separator");

	// replaceCharacters swaps every character found in the set for the
	// replacement string.
	harness.expectTrue(
		StringHelper::replaceCharacters(L"a/b\\c", L"/\\", L"_") == L"a_b_c",
		"replaceCharacters should replace each listed char");
	harness.expectTrue(
		StringHelper::replaceIllegalCharacters(L"na:me?.txt") == L"na_me_.txt",
		"replaceIllegalCharacters should sanitise filename-illegal characters");

	// Case conversion (ASCII).
	harness.expectTrue(StringHelper::toUpperCase(L"Mixed123") == L"MIXED123", "toUpperCase");
	harness.expectTrue(StringHelper::toLowerCase(L"Mixed123") == L"mixed123", "toLowerCase");

	// toWString / toString round trip through UTF-8 (CP_UTF8 == 65001; used as
	// a literal here so this translation unit does not need <windows.h>).
	const unsigned utf8CodePage = 65001u;
	wstring original = L"round-trip 123";
	std::string utf8 = StringHelper::toString(original, utf8CodePage);
	harness.expectTrue(StringHelper::toWString(utf8, utf8CodePage) == original, "UTF-8 round trip should be lossless");

	// splitQuoted treats a quoted span as a single token even with separators
	// inside it.
	vector<wstring> quoted = StringHelper::splitQuoted(L"a \"b c\" d", L' ');
	harness.requireEqual((int)quoted.size(), 3, "splitQuoted token count");
	harness.expectTrue(quoted[1] == L"b c", "quoted span must survive splitQuoted as one token");
}

void testChannelHelper()
{
	// A stereo default mask must name exactly the front-left/right pair.
	int stereoMask = ChannelHelper::getDefaultChannelMask(2);
	vector<wstring> stereo = ChannelHelper::getChannelNames(2, stereoMask);
	harness.requireEqual((int)stereo.size(), 2, "stereo should have two channels");
	harness.expectTrue(stereo[0] == L"L" && stereo[1] == L"R", "stereo channel names should be L, R");

	// A 5.1 default mask must include the canonical surround names.
	int surroundMask = ChannelHelper::getDefaultChannelMask(6);
	vector<wstring> surround = ChannelHelper::getChannelNames(6, surroundMask);
	harness.requireEqual((int)surround.size(), 6, "5.1 should have six channels");
	harness.expectTrue(surround[0] == L"L" && surround[1] == L"R" && surround[2] == L"C" && surround[3] == L"LFE",
		"5.1 channel order should start L, R, C, LFE");

	// getChannelIndex by name, by 1-based number, and out-of-range / alias.
	harness.expectEqual(ChannelHelper::getChannelIndex(L"L", stereo), 0, "L resolves to index 0");
	harness.expectEqual(ChannelHelper::getChannelIndex(L"R", stereo), 1, "R resolves to index 1");
	harness.expectEqual(ChannelHelper::getChannelIndex(L"2", stereo), 1, "channel number 2 resolves to index 1");
	harness.expectEqual(ChannelHelper::getChannelIndex(L"5", stereo, true), -1, "out-of-range channel number is -1");
	// "SUB" is the legacy alias for the LFE channel.
	harness.expectEqual(ChannelHelper::getChannelIndex(L"SUB", surround), 3, "SUB alias resolves to the LFE index");
}
}

void runCommonLogicTests()
{
	testOwningFilterPointer();
	testStringHelper();
	testChannelHelper();
	harness.report();
}
