/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared "LoudnessCorrection:" config-line codec
	(filters/loudnessCorrection/LoudnessCorrectionCommand.{h,cpp}), which the
	engine factory and the Editor GUI both consume.
*/

#include <string>

#include "filters/loudnessCorrection/LoudnessCorrectionCommand.h"
#include "filters/loudnessCorrection/LoudnessCorrectionFilter.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("LoudnessCorrectionCommandTests");

void testCommandRecognition()
{
	LoudnessCorrectionCommand cmd;

	harness.expectTrue(
		LoudnessCorrectionCommand::parse(L"LoudnessCorrection", L"State 1 ReferenceLevel -20 ReferenceOffset 5 Attenuation 0.5", cmd),
		"a complete LoudnessCorrection line is recognized");
	harness.expectTrue(cmd.state, "parsed state");
	harness.expectTrue(cmd.referenceLevel == -20.0f, "parsed reference level");
	harness.expectTrue(cmd.referenceOffset == 5.0f, "parsed reference offset");
	harness.expectTrue(cmd.attenuation == 0.5f, "parsed attenuation");

	harness.expectFalse(
		LoudnessCorrectionCommand::parse(L"Preamp", L"State 1 ReferenceLevel 0 ReferenceOffset 0", cmd),
		"'Preamp' must not parse as LoudnessCorrection");
	harness.expectFalse(
		LoudnessCorrectionCommand::parse(L"loudnesscorrection", L"State 1 ReferenceLevel 0 ReferenceOffset 0", cmd),
		"command match is case-sensitive");
}

void testParameterValidation()
{
	LoudnessCorrectionCommand cmd;

	// Attenuation is optional and falls back to full correction.
	harness.expectTrue(
		LoudnessCorrectionCommand::parse(L"LoudnessCorrection", L"State 0 ReferenceLevel 3 ReferenceOffset -2", cmd),
		"a line without Attenuation parses");
	harness.expectFalse(cmd.state, "parsed state 0");
	harness.expectTrue(cmd.attenuation == 1.0f, "missing attenuation defaults to 1.0");

	// The three other parameters are required.
	harness.expectFalse(
		LoudnessCorrectionCommand::parse(L"LoudnessCorrection", L"ReferenceLevel 0 ReferenceOffset 0", cmd),
		"a line without State is rejected");
	harness.expectFalse(
		LoudnessCorrectionCommand::parse(L"LoudnessCorrection", L"State 1 ReferenceOffset 0", cmd),
		"a line without ReferenceLevel is rejected");
	harness.expectFalse(
		LoudnessCorrectionCommand::parse(L"LoudnessCorrection", L"State 1 ReferenceLevel 0", cmd),
		"a line without ReferenceOffset is rejected");

	// The grammar only accepts attenuation values between 0 and 1; anything
	// else fails the optional match and falls back like a missing parameter.
	harness.expectTrue(
		LoudnessCorrectionCommand::parse(L"LoudnessCorrection", L"State 1 ReferenceLevel 0 ReferenceOffset 0 Attenuation 2.5", cmd),
		"an out-of-range attenuation still parses the line");
	harness.expectTrue(cmd.attenuation == 1.0f, "out-of-range attenuation falls back to 1.0");

	// Audit #348 TD-42: the grammar takes a decimal comma, and wcstod used to
	// stop at it and read the attenuation as 0.
	harness.expectTrue(
		LoudnessCorrectionCommand::parse(L"LoudnessCorrection", L"State 1 ReferenceLevel 0 ReferenceOffset 0 Attenuation 0,5", cmd),
		"a decimal comma attenuation parses");
	harness.expectTrue(cmd.attenuation == 0.5f, "and reads as 0.5, not 0");
}

void testSerialization()
{
	LoudnessCorrectionCommand cmd;
	cmd.state = true;
	cmd.referenceLevel = -20.0f;
	cmd.referenceOffset = 5.0f;
	cmd.attenuation = 1.0f;
	harness.expectTrue(
		cmd.serialize() == L"State 1 ReferenceLevel -20 ReferenceOffset 5 Attenuation 1.0",
		"attenuation 1 keeps the historical '1.0' spelling");

	cmd.attenuation = 0.35f;
	harness.expectTrue(
		cmd.serialize() == L"State 1 ReferenceLevel -20 ReferenceOffset 5 Attenuation 0.35",
		"fractional attenuation uses the shortest form");
}

void testRoundTrip()
{
	const wstring cases[] = {
		L"State 1 ReferenceLevel -20 ReferenceOffset 5 Attenuation 0.5",
		L"State 0 ReferenceLevel 0 ReferenceOffset 0 Attenuation 1.0",
		L"State 1 ReferenceLevel 12 ReferenceOffset -7",
	};

	for (const wstring& parameters : cases)
	{
		LoudnessCorrectionCommand first;
		harness.expectTrue(LoudnessCorrectionCommand::parse(L"LoudnessCorrection", parameters, first), "round-trip input parses");
		wstring serialized = first.serialize();

		LoudnessCorrectionCommand second;
		harness.expectTrue(LoudnessCorrectionCommand::parse(L"LoudnessCorrection", serialized, second), "serialized form parses");
		harness.expectTrue(
			first.state == second.state && first.referenceLevel == second.referenceLevel
			&& first.referenceOffset == second.referenceOffset && first.attenuation == second.attenuation,
			"serialize/parse round trip is stable");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}

// Audit #348 TD-03: at the reference point the preamp was left unwritten, so
// initialize() derived the attenuation from an uninitialised double. The
// three volume regions are pinned here through the pure low-shelf function.
void testLowShelfRegions()
{
	LoudnessCorrectionFilter::FilterParameters parameters;
	parameters.state = true;
	parameters.referenceLevel = -20.0f;
	parameters.referenceOffset = 0.0f;
	parameters.attenuation = 1.0f;

	const LoudnessCorrectionFilter::LowShelf at = LoudnessCorrectionFilter::lowShelfFor(parameters, -20.0);
	harness.expectTrue(at.gain == 0.0 && at.preAmp == 0.0,
		"at the reference point the low shelf applies no gain and no preamp");

	// 10 dB below the reference point: boost 10 * 0.55 / 0.45 dB, preamp = -boost.
	const LoudnessCorrectionFilter::LowShelf below = LoudnessCorrectionFilter::lowShelfFor(parameters, -30.0);
	harness.expectNear(below.gain, 10.0 * 0.55 / 0.45, 1e-12, "below the reference point the low shelf boosts");
	harness.expectNear(below.preAmp, -below.gain, 1e-12, "below the reference point the preamp makes room for the boost");

	// 10 dB above the reference point: a cut and no preamp.
	const LoudnessCorrectionFilter::LowShelf above = LoudnessCorrectionFilter::lowShelfFor(parameters, -10.0);
	harness.expectTrue(above.gain < 0.0, "above the reference point the low shelf cuts");
	harness.expectTrue(above.preAmp == 0.0, "above the reference point there is no preamp");

	// A reading back at the reference point after a lower one must not keep
	// the previous preamp (the update thread reused its variable).
	const LoudnessCorrectionFilter::LowShelf back = LoudnessCorrectionFilter::lowShelfFor(parameters, -20.0);
	harness.expectTrue(back.preAmp == 0.0, "returning to the reference point clears the preamp");
}

// Audit #348 open question (IFilter::initialize contract): a second
// initialize() assigned a new update thread over the running one, which is
// std::terminate. It now stops the first thread and starts over, so the
// second call's channel count is the one process() uses.
void testInitializeTwice()
{
	LoudnessCorrectionFilter::FilterParameters parameters;
	// State 0: process() copies input to output over _channelCount channels,
	// which does not depend on the machine's endpoint volume. The update
	// thread still starts on each initialize(), which is what terminated.
	parameters.state = false;
	parameters.referenceLevel = -20.0f;
	LoudnessCorrectionFilter filter(parameters);

	const std::vector<wstring> stereo = filter.initialize(48000.0f, 4, {L"L", L"R"});
	harness.expectTrue(stereo.size() == 2, "the first initialize keeps two channels");
	const std::vector<wstring> threeChannels = filter.initialize(44100.0f, 4, {L"L", L"R", L"C"});
	harness.expectTrue(threeChannels.size() == 3, "a second initialize runs and keeps three channels");

	double inL[4] = {0.25, -0.5, 0.75, -1.0};
	double inR[4] = {0.1, 0.2, 0.3, 0.4};
	double inC[4] = {-0.1, -0.2, -0.3, -0.4};
	double outL[4] = {};
	double outR[4] = {};
	double outC[4] = {};
	double* input[3] = {inL, inR, inC};
	double* output[3] = {outL, outR, outC};
	filter.process(output, input, 4);

	// The copy covers the channel count of the second call, the third
	// channel included.
	bool unchanged = true;
	for (int c = 0; c < 3; c++)
		for (int n = 0; n < 4; n++)
			unchanged = unchanged && output[c][n] == input[c][n];
	harness.expectTrue(unchanged, "after the second initialize all three channels are processed");
}
}

void runLoudnessCorrectionCommandTests()
{
	testCommandRecognition();
	testParameterValidation();
	testSerialization();
	testRoundTrip();
	testLowShelfRegions();
	testInitializeTwice();

	harness.report();
}
