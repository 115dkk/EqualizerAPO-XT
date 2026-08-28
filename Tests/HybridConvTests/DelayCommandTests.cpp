/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared Delay config-line parser/serializer
	(DelayCommand::parse + DelayCommand::serialize, with the engine's no-op
	gate in DelayFilterFactory::parseCommand on top). They confirm that
	"Delay:" lines parse to the expected delay value and unit, that the cases
	which build no filter (zero delay, unknown unit, negative value) are
	rejected exactly as the engine factory decides, that the codec itself still
	accepts a zero delay (the Editor opens a card for the no-op line), and that
	serializing a parsed command reproduces the canonical "<delay> ms|samples"
	parameter string.

	These tests link against the same Common.lib as HybridConvTests and run from
	its main() via runDelayCommandTests().
*/

#include <string>

#include "filters/DelayCommand.h"
#include "filters/DelayFilterFactory.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("DelayCommandTests");

// Parses "Delay: <parameters>" the way the engine does. Returns whether a
// DelayFilter would be built and, when it would, fills outValue and outIsMs.
bool parseDelay(const wstring& parameters, double& outValue, bool& outIsMs)
{
	DelayCommand cmd;
	if (!DelayFilterFactory::parseCommand(L"Delay", parameters, cmd))
		return false;
	outValue = cmd.delay;
	outIsMs = cmd.isMs;
	return true;
}

void testValueAndUnit()
{
	double value = -1.0;
	bool isMs = false;

	harness.expectTrue(parseDelay(L"100 ms", value, isMs), "'100 ms' should produce a filter");
	harness.expectEqual(value, 100.0, "'100 ms' delay value");
	harness.expectTrue(isMs, "'100 ms' should be milliseconds");

	harness.expectTrue(parseDelay(L"512 samples", value, isMs), "'512 samples' should produce a filter");
	harness.expectEqual(value, 512.0, "'512 samples' delay value");
	harness.expectFalse(isMs, "'512 samples' should be samples, not milliseconds");

	// Fractional millisecond value.
	harness.expectTrue(parseDelay(L"50.5 ms", value, isMs), "'50.5 ms' should produce a filter");
	harness.expectEqual(value, 50.5, "'50.5 ms' delay value");
	harness.expectTrue(isMs, "'50.5 ms' should be milliseconds");

	// Comma decimal mark is normalised to a period before parsing.
	harness.expectTrue(parseDelay(L"50,5 ms", value, isMs), "'50,5 ms' should produce a filter");
	harness.expectEqual(value, 50.5, "comma decimal mark should parse like a period");

	// Unit keyword is matched case-insensitively.
	harness.expectTrue(parseDelay(L"100 MS", value, isMs), "'100 MS' should produce a filter");
	harness.expectTrue(isMs, "'100 MS' should be milliseconds");
	harness.expectTrue(parseDelay(L"512 SAMPLES", value, isMs), "'512 SAMPLES' should produce a filter");
	harness.expectFalse(isMs, "'512 SAMPLES' should be samples");
}

void testRejectedCommands()
{
	double value = -1.0;
	bool isMs = false;

	// A zero-length delay is a no-op and builds no filter.
	harness.expectFalse(parseDelay(L"0 ms", value, isMs), "'0 ms' is a no-op and should build no filter");
	harness.expectFalse(parseDelay(L"0 samples", value, isMs), "'0 samples' is a no-op and should build no filter");

	// An unknown unit is rejected.
	harness.expectFalse(parseDelay(L"100 foo", value, isMs), "unknown unit should be rejected");

	// A missing unit is rejected (only a number, no recognised keyword).
	harness.expectFalse(parseDelay(L"100", value, isMs), "missing unit should be rejected");

	// A negative delay is rejected.
	harness.expectFalse(parseDelay(L"-5 ms", value, isMs), "negative delay should be rejected");
}

// The codec itself accepts a zero delay: the engine's no-op gate lives in the
// factory wrapper only, so the Editor can still open a knob card for the
// insert template's own "Delay: 0 ms" instead of dropping the row to raw text.
void testCodecAcceptsZero()
{
	DelayCommand cmd;
	harness.expectTrue(DelayCommand::parse(L"0 ms", cmd), "codec should accept '0 ms'");
	harness.expectEqual(cmd.delay, 0.0, "'0 ms' codec delay value");
	harness.expectTrue(cmd.isMs, "'0 ms' codec unit flag");
	harness.expectTrue(cmd.serialize() == L"0 ms", "zero delay should serialize back to '0 ms'");

	harness.expectTrue(DelayCommand::parse(L"0 samples", cmd), "codec should accept '0 samples'");
	harness.expectEqual(cmd.delay, 0.0, "'0 samples' codec delay value");
	harness.expectFalse(cmd.isMs, "'0 samples' codec unit flag");

	// The codec still rejects what is not a delay line at all.
	harness.expectFalse(DelayCommand::parse(L"0 foo", cmd), "codec should reject an unknown unit");
	harness.expectFalse(DelayCommand::parse(L"-1 ms", cmd), "codec should reject a negative delay");
}

// Asserts that parsing line then serializing the resulting command reproduces
// the expected canonical parameter string.
void expectRoundTrip(const wstring& parameters, const wstring& expected)
{
	DelayCommand cmd;
	harness.expectTrue(DelayFilterFactory::parseCommand(L"Delay", parameters, cmd),
		"round-trip input should parse");
	harness.expectTrue(cmd.serialize() == expected,
		"serialize(parse(\"" + std::string(parameters.begin(), parameters.end()) + "\")) mismatch");
}

void testSerializeRoundTrip()
{
	// The canonical forms serialize back to themselves.
	expectRoundTrip(L"100 ms", L"100 ms");
	expectRoundTrip(L"512 samples", L"512 samples");
	expectRoundTrip(L"50.5 ms", L"50.5 ms");

	// Normalised / case-folded inputs serialize to the canonical text.
	expectRoundTrip(L"50,5 ms", L"50.5 ms");
	expectRoundTrip(L"100 MS", L"100 ms");
	expectRoundTrip(L"512 SAMPLES", L"512 samples");

	// Serializing a hand-built command produces the same canonical string, and a
	// second parse of that string yields the same value and unit (a full
	// command -> string -> command round trip).
	DelayCommand built;
	built.delay = 250.0;
	built.isMs = true;
	wstring serialized = built.serialize();
	harness.expectTrue(serialized == L"250 ms", "hand-built command should serialize to '250 ms'");

	DelayCommand reparsed;
	harness.expectTrue(DelayFilterFactory::parseCommand(L"Delay", serialized, reparsed),
		"serialized command should parse back");
	harness.expectEqual(reparsed.delay, built.delay, "round-trip delay value");
	harness.expectTrue(reparsed.isMs == built.isMs, "round-trip unit flag");
}
}

void runDelayCommandTests()
{
	testValueAndUnit();
	testRejectedCommands();
	testCodecAcceptsZero();
	testSerializeRoundTrip();
	harness.report();
}
