/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared GraphicEQ config-line parser/serializer
	(GraphicEQCommand::parse + GraphicEQCommand::serialize). They confirm that
	"GraphicEQ:" node lists parse to the expected (freq, dB-gain) pairs - including
	many bands, mixed frequencies and decimal gains - that the nodes come out
	sorted by frequency, that an unpaired trailing number is dropped, that a
	comma decimal mark is accepted, and that serializing a parsed command
	reproduces the canonical "<freq> <gain>; ..." parameter string.

	These tests link against the same Common.lib as HybridConvTests and run from
	its main() via runGraphicEQCommandTests().
*/

#include <string>

#include "filters/GraphicEQCommand.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("GraphicEQCommandTests");

// Parses a "GraphicEQ:" parameter string into a command, the same way the engine
// factory and the Editor GUI factory do.
GraphicEQCommand parse(const wstring& parameters)
{
	GraphicEQCommand cmd;
	cmd.parse(parameters);
	return cmd;
}

void expectNode(const GraphicEQCommand& cmd, size_t index, double freq, double gain, const std::string& label)
{
	harness.require(index < cmd.nodes.size(), label + ": node index out of range");
	harness.expectEqual(cmd.nodes[index].freq, freq, label + " freq");
	harness.expectEqual(cmd.nodes[index].dbGain, gain, label + " gain");
}

void testBasicParse()
{
	// A small list with integer frequencies and signed integer / decimal gains.
	GraphicEQCommand cmd = parse(L"25 -3; 100 0; 1000 6; 16000 -1.5");
	harness.expectEqual(cmd.nodes.size(), (size_t)4, "four-node list size");
	expectNode(cmd, 0, 25.0, -3.0, "node 0");
	expectNode(cmd, 1, 100.0, 0.0, "node 1");
	expectNode(cmd, 2, 1000.0, 6.0, "node 2");
	expectNode(cmd, 3, 16000.0, -1.5, "node 3");
}

void testManyBands()
{
	// The 15-band template line. All gains are zero; frequencies span 25..16000.
	GraphicEQCommand cmd = parse(L"25 0; 40 0; 63 0; 100 0; 160 0; 250 0; 400 0; 630 0; 1000 0; 1600 0; 2500 0; 4000 0; 6300 0; 10000 0; 16000 0");
	harness.expectEqual(cmd.nodes.size(), (size_t)15, "15-band list size");
	expectNode(cmd, 0, 25.0, 0.0, "15-band first");
	expectNode(cmd, 14, 16000.0, 0.0, "15-band last");

	// A 31-band line that includes a fractional centre frequency (31.5).
	GraphicEQCommand wide = parse(L"20 0; 25 0; 31.5 0; 40 0; 50 0; 63 0; 80 0; 100 0; 125 0; 160 0; 200 0; 250 0; 315 0; 400 0; 500 0; 630 0; 800 0; 1000 0; 1250 0; 1600 0; 2000 0; 2500 0; 3150 0; 4000 0; 5000 0; 6300 0; 8000 0; 10000 0; 12500 0; 16000 0; 20000 0");
	harness.expectEqual(wide.nodes.size(), (size_t)31, "31-band list size");
	expectNode(wide, 2, 31.5, 0.0, "31-band fractional centre");
	expectNode(wide, 30, 20000.0, 0.0, "31-band last");
}

void testDecimalsAndSorting()
{
	// Decimal frequencies and gains with two decimal places, given out of order:
	// the parser must sort the nodes by frequency.
	GraphicEQCommand cmd = parse(L"1000 -2.25; 31.5 3.5; 250 0.07; 100 1.5");
	harness.expectEqual(cmd.nodes.size(), (size_t)4, "decimal list size");
	expectNode(cmd, 0, 31.5, 3.5, "sorted node 0");
	expectNode(cmd, 1, 100.0, 1.5, "sorted node 1");
	expectNode(cmd, 2, 250.0, 0.07, "sorted node 2");
	expectNode(cmd, 3, 1000.0, -2.25, "sorted node 3");
}

void testUnpairedAndComma()
{
	// A trailing frequency with no matching gain is dropped (matches the factory's
	// freq/gain pairing).
	GraphicEQCommand cmd = parse(L"100 1; 1000 2; 5000");
	harness.expectEqual(cmd.nodes.size(), (size_t)2, "unpaired trailing number dropped");
	expectNode(cmd, 0, 100.0, 1.0, "paired node 0");
	expectNode(cmd, 1, 1000.0, 2.0, "paired node 1");

	// With no period anywhere in the string, commas are treated as decimal marks
	// before parsing, so "31,5 1,5" reads as freq 31.5, gain 1.5.
	GraphicEQCommand comma = parse(L"31,5 1,5");
	harness.expectEqual(comma.nodes.size(), (size_t)1, "comma decimal list size");
	expectNode(comma, 0, 31.5, 1.5, "comma decimal node");

	// An empty parameter (the "variable bands" template) parses to no nodes.
	GraphicEQCommand empty = parse(L"");
	harness.expectEqual(empty.nodes.size(), (size_t)0, "empty parameter has no nodes");

	GraphicEQCommand nonFinite = parse(L"100 1e999; 1000 2");
	harness.expectEqual(nonFinite.nodes.size(), (size_t)1,
		"a non-finite GraphicEQ pair is rejected without dropping valid pairs");
	expectNode(nonFinite, 0, 1000.0, 2.0, "finite node after rejected pair");
}

// Asserts that parsing parameters then serializing the command reproduces the
// expected canonical parameter string.
void expectRoundTrip(const wstring& parameters, const wstring& expected)
{
	GraphicEQCommand cmd = parse(parameters);
	harness.expectTrue(cmd.serialize() == expected,
		"serialize(parse(\"" + std::string(parameters.begin(), parameters.end()) + "\")) mismatch");
}

void testSerializeRoundTrip()
{
	// Canonical forms serialize back to themselves.
	expectRoundTrip(L"25 0; 100 6; 16000 -1.5", L"25 0; 100 6; 16000 -1.5");
	expectRoundTrip(L"31.5 3.5; 100 1.5; 250 0.07; 1000 -2.25", L"31.5 3.5; 100 1.5; 250 0.07; 1000 -2.25");

	// Out-of-order input serializes in frequency order (parse sorts).
	expectRoundTrip(L"1000 -2.25; 31.5 3.5; 100 1.5; 250 0.07", L"31.5 3.5; 100 1.5; 250 0.07; 1000 -2.25");

	// Comma decimal marks normalise to periods in the serialized text.
	expectRoundTrip(L"31,5 1,5", L"31.5 1.5");

	// An empty list serializes to an empty string.
	expectRoundTrip(L"", L"");

	// Serializing a hand-built command produces the canonical string, and parsing
	// that string back yields the same node values (a full command -> string ->
	// command round trip).
	GraphicEQCommand built;
	built.nodes.push_back(FilterNode(63.0, -4.0));
	built.nodes.push_back(FilterNode(2500.0, 2.5));
	wstring serialized = built.serialize();
	harness.expectTrue(serialized == L"63 -4; 2500 2.5", "hand-built command should serialize to '63 -4; 2500 2.5'");

	GraphicEQCommand reparsed = parse(serialized);
	harness.expectEqual(reparsed.nodes.size(), built.nodes.size(), "round-trip node count");
	expectNode(reparsed, 0, 63.0, -4.0, "round-trip node 0");
	expectNode(reparsed, 1, 2500.0, 2.5, "round-trip node 1");
}
}

void runGraphicEQCommandTests()
{
	testBasicParse();
	testManyBands();
	testDecimalsAndSorting();
	testUnpairedAndComma();
	testSerializeRoundTrip();
	harness.report();
}
