/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared "Filter:" IIR config-line codec
	(filters/IIRCommand.{h,cpp} + IIRFilterFactory::parseCommand), which the
	engine factory consumes.
*/

#include <string>
#include <vector>

#include "filters/IIRCommand.h"
#include "filters/IIRFilterFactory.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("IIRCommandTests");

void testCommandRecognition()
{
	IIRCommand cmd;

	harness.expectTrue(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Order 1 Coefficients 1 0 0.5 0", cmd),
		"'Filter' IIR line is recognized");
	harness.expectTrue(
		IIRFilterFactory::parseCommand(L"Filter1", L"ON IIR Order 1 Coefficients 1 0 0.5 0", cmd),
		"numbered 'Filter1' command is recognized");

	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Preamp", L"ON IIR Order 1 Coefficients 1 0 0.5 0", cmd),
		"'Preamp' must not parse as IIR");
	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"ON PK Fc 1000 Hz Gain 3 dB Q 1.41", cmd),
		"BiQuad lines must not parse as IIR");
	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"Order 1 Coefficients 1 0 0.5 0", cmd),
		"a line without 'ON IIR' must not parse");
}

void testParameterValidation()
{
	IIRCommand cmd;

	harness.expectTrue(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Order 2 Coefficients 1 0 0 0.5 0 0", cmd),
		"order 2 with 6 coefficients parses");
	harness.expectEqual(cmd.order, 2u, "parsed order");
	harness.requireEqual(cmd.coefficients.size(), (size_t)6, "parsed coefficient count");
	harness.expectTrue(cmd.coefficients[0] == 1.0 && cmd.coefficients[3] == 0.5, "coefficient values");

	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Order 0 Coefficients 1 0", cmd),
		"order 0 is rejected");
	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Order 1 Coefficients 1 0 0.5", cmd),
		"wrong coefficient count is rejected");
	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Coefficients 1 0 0.5 0", cmd),
		"a line without Order is rejected");
	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Order 1", cmd),
		"a line without Coefficients is rejected");
	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Order 1 Coefficients 1 0 0 0", cmd),
		"a zero a0 coefficient is rejected");
	harness.expectFalse(
		IIRFilterFactory::parseCommand(L"Filter", L"ON IIR Order 1 Coefficients 1e999 0 1 0", cmd),
		"non-finite coefficients are rejected");
}

void testRoundTrip()
{
	const wstring cases[] = {
		L"ON IIR Order 1 Coefficients 1 0 0.5 0",
		L"  ON IIR  Order 2  Coefficients 1 -0.5 0.25 1 0 0",
		L"ON IIR Order 1 Coefficients 1e-05 2.5E3 -1.5 +0.125",
	};

	for (const wstring& parameters : cases)
	{
		IIRCommand first;
		harness.expectTrue(IIRFilterFactory::parseCommand(L"Filter", parameters, first), "round-trip input parses");
		wstring serialized = first.serialize();

		IIRCommand second;
		harness.expectTrue(IIRFilterFactory::parseCommand(L"Filter", serialized, second), "serialized form parses");
		harness.expectEqual(second.order, first.order, "round trip keeps the order");
		harness.expectTrue(first.coefficients == second.coefficients, "serialize/parse round trip is stable");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}
}

void runIIRCommandTests()
{
	testCommandRecognition();
	testParameterValidation();
	testRoundTrip();

	harness.report();
}
