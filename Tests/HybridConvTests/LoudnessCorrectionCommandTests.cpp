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
}

void runLoudnessCorrectionCommandTests()
{
	testCommandRecognition();
	testParameterValidation();
	testSerialization();
	testRoundTrip();

	harness.report();
}
