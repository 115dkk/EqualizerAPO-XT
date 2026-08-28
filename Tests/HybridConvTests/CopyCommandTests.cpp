/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared Copy config-line parser/serializer
	(parseCopyAssignments + serializeCopyAssignments in filters/CopyFilter.cpp).
	They confirm that representative "Copy:" lines - simple identity routing,
	crossfeed with factors, summands, dB factors, constant values and virtual
	channels - parse to the expected std::vector<Assignment> (the same type
	CopyFilter::getAssignments() returns), that malformed chunks are dropped
	exactly as the engine factory drops them, and that serializing a parsed set
	of assignments reproduces the canonical parameter string so that
	serialize(parse(line)) round-trips.

	These tests link against the same Common.lib as HybridConvTests and run from
	its main() via runCopyCommandTests().
*/

#include <string>
#include <vector>

#include "filters/CopyFilter.h"
#include "Tests/TestHarness.h"

using std::vector;
using std::wstring;

namespace
{
test::Harness harness("CopyCommandTests");

// Convenience accessor for one summand of one assignment, with bounds checks
// folded into the harness so a malformed parse fails loudly instead of reading
// out of range.
const Assignment::Summand& summandAt(const vector<Assignment>& assignments, size_t a, size_t s, const std::string& label)
{
	harness.require(a < assignments.size(), label + ": assignment index out of range");
	harness.require(s < assignments[a].sourceSum.size(), label + ": summand index out of range");
	return assignments[a].sourceSum[s];
}

void expectSummand(const vector<Assignment>& assignments, size_t a, size_t s,
		const wstring& channel, double factor, bool isDecibel, const std::string& label)
{
	const Assignment::Summand& summand = summandAt(assignments, a, s, label);
	harness.expectTrue(summand.channel == channel, label + " channel");
	harness.expectEqual(summand.factor, factor, label + " factor");
	harness.expectTrue(summand.isDecibel == isDecibel, label + " isDecibel");
}

void testSimpleRouting()
{
	// Plain identity routing: each target reads one source channel at unit gain.
	vector<Assignment> assignments = parseCopyAssignments(L"L=R C=C");
	harness.requireEqual(assignments.size(), (size_t)2, "two-assignment list size");
	harness.expectTrue(assignments[0].targetChannel == L"L", "first target is L");
	harness.expectEqual(assignments[0].sourceSum.size(), (size_t)1, "L has one summand");
	expectSummand(assignments, 0, 0, L"R", 1.0, false, "L=R summand");
	harness.expectTrue(assignments[1].targetChannel == L"C", "second target is C");
	expectSummand(assignments, 1, 0, L"C", 1.0, false, "C=C summand");
}

void testCrossfeedWithFactors()
{
	// Mono downmix: both outputs are a weighted sum of L and R.
	vector<Assignment> assignments = parseCopyAssignments(L"L=0.5*L+0.5*R R=0.5*L+0.5*R");
	harness.requireEqual(assignments.size(), (size_t)2, "downmix assignment count");
	harness.expectEqual(assignments[0].sourceSum.size(), (size_t)2, "L has two summands");
	expectSummand(assignments, 0, 0, L"L", 0.5, false, "L summand 0");
	expectSummand(assignments, 0, 1, L"R", 0.5, false, "L summand 1");
	expectSummand(assignments, 1, 0, L"L", 0.5, false, "R summand 0");
	expectSummand(assignments, 1, 1, L"R", 0.5, false, "R summand 1");
}

void testNegativeFactorAndVirtualChannel()
{
	// A virtual upmix channel built from a negative-weighted source sum. The
	// leading "0.866" carries a '.' so it is read as a factor, and "-0.5" is a
	// negative factor on the second summand.
	vector<Assignment> assignments = parseCopyAssignments(L"VSL=0.866*L+-0.5*R");
	harness.requireEqual(assignments.size(), (size_t)1, "virtual channel assignment count");
	harness.expectTrue(assignments[0].targetChannel == L"VSL", "target is virtual VSL");
	expectSummand(assignments, 0, 0, L"L", 0.866, false, "VSL summand 0");
	expectSummand(assignments, 0, 1, L"R", -0.5, false, "VSL summand 1");
}

void testDecibelFactor()
{
	// A dB-suffixed factor sets the isDecibel flag; the magnitude is the raw dB
	// number (the engine converts it to a linear gain in CopyFilter::initialize).
	vector<Assignment> assignments = parseCopyAssignments(L"L=-3dB*R");
	harness.expectEqual(assignments.size(), (size_t)1, "dB assignment count");
	expectSummand(assignments, 0, 0, L"R", -3.0, true, "dB summand");
}

void testConstantValue()
{
	// A lone "0" with no channel is a constant value, not a channel reference.
	vector<Assignment> assignments = parseCopyAssignments(L"LFE=0");
	harness.expectEqual(assignments.size(), (size_t)1, "constant assignment count");
	const Assignment::Summand& summand = summandAt(assignments, 0, 0, "constant summand");
	harness.expectTrue(summand.channel == L"", "constant has no source channel");
	harness.expectEqual(summand.factor, 0.0, "constant factor value");
	harness.expectFalse(summand.isDecibel, "constant is not a dB value");
}

void testMalformedChunkDropped()
{
	// A token with no '=' is not an assignment and is dropped, matching the
	// engine factory; the well-formed assignments around it survive.
	vector<Assignment> assignments = parseCopyAssignments(L"L=R garbage C=C");
	harness.requireEqual(assignments.size(), (size_t)2, "malformed chunk is dropped");
	harness.expectTrue(assignments[0].targetChannel == L"L", "surviving target L");
	harness.expectTrue(assignments[1].targetChannel == L"C", "surviving target C");

	// An empty parameter (the "Copy: " template line) parses to no assignments.
	vector<Assignment> empty = parseCopyAssignments(L"");
	harness.expectEqual(empty.size(), (size_t)0, "empty parameter has no assignments");

	vector<Assignment> nonFinite = parseCopyAssignments(L"L=1.0e999*R");
	harness.expectEqual(nonFinite.size(), (size_t)0,
		"an assignment with a non-finite Copy factor is rejected");
}

// Asserts that parsing parameters then serializing the assignments reproduces
// the expected canonical parameter string.
void expectRoundTrip(const wstring& parameters, const wstring& expected)
{
	vector<Assignment> assignments = parseCopyAssignments(parameters);
	wstring serialized = serializeCopyAssignments(assignments);
	harness.expectTrue(serialized == expected,
		"serialize(parse(\"" + std::string(parameters.begin(), parameters.end()) + "\")) mismatch");

	// A second parse/serialize cycle must be stable (the canonical form is a fixed
	// point of the parser/serializer pair).
	vector<Assignment> reparsed = parseCopyAssignments(serialized);
	harness.expectTrue(serializeCopyAssignments(reparsed) == expected,
		"second round-trip of \"" + std::string(parameters.begin(), parameters.end()) + "\" is not stable");
}

void testSerializeRoundTrip()
{
	// Canonical forms serialize back to themselves.
	expectRoundTrip(L"L=R C=C", L"L=R C=C");
	expectRoundTrip(L"L=0.5*L+0.5*R R=0.5*L+0.5*R", L"L=0.5*L+0.5*R R=0.5*L+0.5*R");
	expectRoundTrip(L"VSL=0.866*L+-0.5*R", L"VSL=0.866*L+-0.5*R");
	expectRoundTrip(L"LFE=0", L"LFE=0");

	// Virtual channels used as both targets and sources survive a round trip.
	expectRoundTrip(L"VSL=L VSR=R L=VSL+VSR", L"VSL=L VSR=R L=VSL+VSR");

	// A bare integer factor is re-emitted with a ".0" suffix so it stays a factor
	// (not a channel name) on the next parse; "-3dB" canonicalises to "-3.0dB".
	expectRoundTrip(L"L=2*R", L"L=2.0*R");
	expectRoundTrip(L"L=-3dB*R", L"L=-3.0dB*R");

	// An empty list serializes to an empty string.
	expectRoundTrip(L"", L"");

	// A hand-built set of assignments serializes to the canonical string, and
	// parsing that string back yields the same channels, factors and flags (a full
	// assignments -> string -> assignments round trip).
	vector<Assignment> built;
	{
		Assignment a;
		a.targetChannel = L"SL";
		Assignment::Summand s;
		s.channel = L"L";
		s.factor = 0.7;
		s.isDecibel = false;
		a.sourceSum.push_back(s);
		built.push_back(a);
	}
	wstring serialized = serializeCopyAssignments(built);
	harness.expectTrue(serialized == L"SL=0.7*L", "hand-built assignment should serialize to 'SL=0.7*L'");

	vector<Assignment> reparsed = parseCopyAssignments(serialized);
	harness.requireEqual(reparsed.size(), (size_t)1, "round-trip assignment count");
	harness.expectTrue(reparsed[0].targetChannel == L"SL", "round-trip target");
	expectSummand(reparsed, 0, 0, L"L", 0.7, false, "round-trip summand");
}

void testEmptySumAssignmentsSerializeToNothing()
{
	// The Editor's routing views seed one empty assignment per device channel
	// so an emptied Copy stays editable in the GUI; those seeded rows must
	// vanish when the line is written back.
	vector<Assignment> assignments = parseCopyAssignments(L"L=R");
	Assignment seeded;
	seeded.targetChannel = L"C";
	assignments.push_back(seeded);
	harness.expectTrue(serializeCopyAssignments(assignments) == L"L=R",
		"an assignment with an empty source sum serializes to nothing");

	vector<Assignment> onlySeeded;
	onlySeeded.push_back(seeded);
	harness.expectTrue(serializeCopyAssignments(onlySeeded) == L"",
		"a lone empty-sum assignment serializes to an empty string");
}
void testPropagateChannels()
{
	vector<wstring> channels = {L"L", L"R", L"LFE"};
	vector<Assignment> assignments = parseCopyAssignments(L"V1=L V2=0 SUB=R");

	Assignment unfinished;
	unfinished.targetChannel = L"UNFINISHED";
	Assignment::Summand sentinel;
	sentinel.channel = L" ";
	unfinished.sourceSum.push_back(sentinel);
	assignments.push_back(unfinished);

	Assignment empty;
	empty.targetChannel = L"EMPTY";
	assignments.push_back(empty);

	propagateCopyChannels(assignments, channels);
	harness.expectTrue(channels == vector<wstring>({L"L", L"R", L"LFE", L"V1", L"V2"}),
		"Copy channel propagation appends valid targets in order, keeps LFE/SUB aliases unique, and skips unfinished rows");

	propagateCopyChannels(assignments, channels);
	harness.expectTrue(channels == vector<wstring>({L"L", L"R", L"LFE", L"V1", L"V2"}),
		"Copy channel propagation is idempotent");

	vector<wstring> chained = {L"L", L"R"};
	propagateCopyChannels(parseCopyAssignments(L"V1=L"), chained);
	propagateCopyChannels(parseCopyAssignments(L"V2=V1"), chained);
	harness.expectTrue(chained == vector<wstring>({L"L", L"R", L"V1", L"V2"}),
		"successive Copy commands expose virtual targets to the commands below them");
}

void testConstantCopyPreservesTrueSilence()
{
	CopyFilter filter(parseCopyAssignments(L"L=0.5"));
	filter.initialize(48000.0f, 4, vector<wstring>{ L"L", L"R" });

	double left[4] = {};
	double right[4] = {};
	double output[4] = { 9.0, 9.0, 9.0, 9.0 };
	double* inputs[] = { left, right };
	double* outputs[] = { output };

	filter.process(outputs, inputs, 4);
	for (double sample : output)
		harness.expectEqual(sample, 0.0, "true silence remains silent with a constant Copy summand");
	harness.expectFalse(filter.producesTailFromSilentInput(),
		"constant Copy does not claim a tail from silent input");

	// cppcheck-suppress unreadVariable
	right[0] = 1.0; // Observed indirectly through inputs by the silence detector.
	filter.process(outputs, inputs, 4);
	for (double sample : output)
		harness.expectEqual(sample, 0.5, "constant Copy remains available when the input carries a signal");
}

}

void runCopyCommandTests()
{
	testSimpleRouting();
	testCrossfeedWithFactors();
	testNegativeFactorAndVirtualChannel();
	testDecibelFactor();
	testConstantValue();
	testMalformedChunkDropped();
	testSerializeRoundTrip();
	testEmptySumAssignmentsSerializeToNothing();
	testPropagateChannels();
	testConstantCopyPreservesTrueSilence();
	harness.report();
}
