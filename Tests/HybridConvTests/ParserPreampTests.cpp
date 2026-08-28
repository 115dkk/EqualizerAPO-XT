/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the "Preamp:" config-line grammar. These exercise the
	single owning parse routine PreampFilterFactory::parseCommand (which both the
	engine factory and the Editor GUI now share) and serializePreampCommand, the
	canonical "<dB> dB" serializer that PreampFilterGUI::store() routes through.

	The tests assert three things:
	  - parseCommand fills the PreampCommand with the expected dB value and the
	    valid / noOp flags the engine relies on (0 dB is a valid no-op; a
	    malformed parameter is not valid at all),
	  - serialize(parse(line)) reproduces the canonical parameter string, so a
	    line that survives the GUI round trip comes back unchanged, and
	  - that round trip is idempotent, so repeatedly opening and saving a config
	    cannot drift the gain. The number grammar the engine accepts is wider
	    than the canonical form it writes back (decimal comma, exponent
	    notation, surrounding whitespace, a missing "dB"), and every one of those
	    forms has to land on the same canonical string.

	Like the other suites in this binary, this is framework-free and shares the
	Tests/TestHarness.h harness; it links the same Common.lib.
*/

#include <string>
#include "text/WideString.h"
#include "parser/NumericText.h"

#include "filters/PreampFilterFactory.h"
#include "filters/PreampCommand.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("ParserPreampTests");

// Parses a single "Preamp:" line. The command keyword is always "Preamp" here;
// the factory splits command from parameters at the ':' before calling
// parseCommand, so the test passes them already separated.
PreampCommand parsePreamp(const wstring& parameters)
{
	wstring command = L"Preamp";
	PreampCommand cmd;
	// The keyword is recognized, so parseCommand returns true; the valid/noOp
	// flags then describe the parse outcome.
	bool recognized = PreampFilterFactory::parseCommand(command, parameters, cmd);
	harness.expectTrue(recognized, "Preamp keyword should be recognized by parseCommand");
	return cmd;
}

// Asserts the parsed dB plus that serialize(parse) reproduces the expected
// canonical parameter string.
void expectRoundTrip(const wstring& parameters, double expectedDb, const wstring& expectedSerialized)
{
	PreampCommand cmd = parsePreamp(parameters);
	harness.expectTrue(cmd.valid, "well-formed preamp parameter should parse as valid");
	harness.expectTrue(cmd.dbGain == expectedDb, "parsed preamp dB value mismatch");
	harness.expectTrue(cmd.serialize() == expectedSerialized,
		"serialize(parse(line)) should reproduce the canonical parameter string");
}

// Second-generation stability: the canonical string a save writes back has to
// parse to the same gain and re-serialize byte for byte. Without this, every
// open/save cycle could nudge the value, which is how a rounded formatter
// turns -6.25 dB into -6.3 dB and then into -6 dB.
void expectSerializeIsIdempotent(const wstring& parameters)
{
	PreampCommand first = parsePreamp(parameters);
	// Gate: the rest of this check reads first.dbGain and compares serialized
	// forms, which is meaningless if the input never parsed.
	harness.require(first.valid, "idempotence input should parse as valid");
	const wstring once = first.serialize();

	PreampCommand second = parsePreamp(once);
	harness.expectTrue(second.valid, "the serialized form should parse again");
	harness.expectTrue(second.dbGain == first.dbGain, "re-parsing the serialized form should keep the dB value");
	harness.expectTrue(second.serialize() == once, "serializing twice should produce the identical string");
}
}

void runParserPreampTests()
{
	// Canonical lines: serialize reproduces the input parameter exactly.
	expectRoundTrip(L"0 dB", 0.0, L"0 dB");
	expectRoundTrip(L"-6 dB", -6.0, L"-6 dB");
	expectRoundTrip(L"6 dB", 6.0, L"6 dB");
	expectRoundTrip(L"-6.5 dB", -6.5, L"-6.5 dB");
	expectRoundTrip(L"12.34 dB", 12.34, L"12.34 dB");

	// 0 dB is valid but flagged as a no-op: the engine factory builds no
	// PreampFilter for it, while the Editor still shows its GUI.
	PreampCommand zero = parsePreamp(L"0 dB");
	harness.expectTrue(zero.valid, "0 dB should parse as valid");
	harness.expectTrue(zero.noOp, "0 dB should be flagged as a no-op");

	// A non-zero gain is valid and not a no-op.
	PreampCommand nonZero = parsePreamp(L"-6 dB");
	harness.expectTrue(nonZero.valid, "-6 dB should parse as valid");
	harness.expectFalse(nonZero.noOp, "-6 dB should not be a no-op");

	// Comma decimal mark is normalized to a period before parsing, matching the
	// engine factory (numeric_text::normalizeDecimalComma), and serializes back
	// with a period.
	expectRoundTrip(L"-6,5 dB", -6.5, L"-6.5 dB");
	// The engine hands over line.substr(pos + 1) without trimming, so the
	// comma form has to survive with the separator's space still attached.
	expectRoundTrip(L" -6,5 dB", -6.5, L"-6.5 dB");

	// Leading whitespace is tolerated by the " %lf dB" scan format.
	expectRoundTrip(L"  3 dB", 3.0, L"3 dB");
	expectRoundTrip(L"  -6.5 dB  ", -6.5, L"-6.5 dB");

	// Exponent notation: swscanf_s' %lf accepts the same subject sequence as
	// strtod, so the engine reads these as ordinary numbers. They serialize back
	// in plain form because %g only switches to an exponent below 1e-5 or at
	// six digits and up.
	expectRoundTrip(L"-1.5e1 dB", -15.0, L"-15 dB");
	expectRoundTrip(L"2.5E-1 dB", 0.25, L"0.25 dB");

	// Fractional dB must survive intact. %g keeps six significant digits, so a
	// quarter-dB step stays a quarter-dB step; a two-decimal or rounding
	// formatter would turn -6.25 into -6.3 and lose the user's setting.
	expectRoundTrip(L"-6.25 dB", -6.25, L"-6.25 dB");
	expectRoundTrip(L"3.75 dB", 3.75, L"3.75 dB");
	expectRoundTrip(L"-0.05 dB", -0.05, L"-0.05 dB");
	expectRoundTrip(L"1.23456 dB", 1.23456, L"1.23456 dB");

	// The unit is decorative: swscanf_s counts assignments, so the %lf match
	// alone makes the line valid and a missing "dB" is not an error today.
	PreampCommand withoutUnit = parsePreamp(L"-4.5");
	harness.expectTrue(withoutUnit.valid, "the engine accepts a bare number; the \"dB\" suffix is not required to parse");
	harness.expectTrue(withoutUnit.dbGain == -4.5, "a bare number parses as the dB value");
	harness.expectTrue(withoutUnit.serialize() == L"-4.5 dB", "serialize always emits the canonical \"<dB> dB\" form");

	// Repeated open/save cycles must not drift the value. Covers the plain,
	// fractional, comma, exponent and %g-exponent-output forms.
	expectSerializeIsIdempotent(L"0 dB");
	expectSerializeIsIdempotent(L"-6 dB");
	expectSerializeIsIdempotent(L"-6.25 dB");
	expectSerializeIsIdempotent(L"12.34 dB");
	expectSerializeIsIdempotent(L"1.23456 dB");
	expectSerializeIsIdempotent(L"-6,5 dB");
	expectSerializeIsIdempotent(L"-1.5e1 dB");
	// Small enough that %g itself emits an exponent, so this checks the
	// serializer's own output parses back rather than only the inputs above.
	expectSerializeIsIdempotent(L"1e-5 dB");

	// Malformed parameter: keyword is recognized but the value does
	// not parse, so valid stays false and the engine applies no preamp.
	PreampCommand bad = parsePreamp(L"loud please");
	harness.expectFalse(bad.valid, "a non-numeric preamp parameter must not parse as valid");
	harness.expectFalse(bad.noOp, "a malformed preamp parameter is not a no-op");

	PreampCommand nonFinite = parsePreamp(L"1e999 dB");
	harness.expectFalse(nonFinite.valid, "a non-finite preamp value must not parse as valid");

	// A non-"Preamp" command is rejected outright (returns false, leaves the
	// struct in its default state).
	{
		wstring command = L"Delay";
		wstring parameters = L"5 ms";
		PreampCommand cmd;
		harness.expectFalse(PreampFilterFactory::parseCommand(command, parameters, cmd),
			"parseCommand should reject a non-Preamp command");
		harness.expectFalse(cmd.valid, "a rejected command leaves the struct invalid");
	}

	harness.report();
}
