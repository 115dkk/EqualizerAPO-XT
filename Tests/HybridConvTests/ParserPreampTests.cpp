/*
	This file is part of EqualizerAPO-XT.

	Round-trip tests for the "Preamp:" config-line grammar. These exercise the
	single owning parse routine PreampFilterFactory::parseCommand (which both the
	engine factory and the Editor GUI now share) and serializePreampCommand, the
	canonical "<dB> dB" serializer that PreampFilterGUI::store() routes through.

	The tests assert two things for each line:
	  - parseCommand fills the PreampCommand with the expected dB value and the
	    valid / noOp flags the engine relies on (0 dB is a valid no-op; a
	    malformed parameter is not valid at all), and
	  - serialize(parse(line)) reproduces the canonical parameter string, so a
	    line that survives the GUI round trip comes back unchanged.

	Like the other suites in this binary, this is framework-free and shares the
	Tests/TestHarness.h harness; it links the same Common.lib.
*/

#include <string>

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
	// engine factory (StringHelper::replaceCharacters), and serializes back with
	// a period.
	expectRoundTrip(L"-6,5 dB", -6.5, L"-6.5 dB");

	// Leading whitespace is tolerated by the " %lf dB" scan format.
	expectRoundTrip(L"  3 dB", 3.0, L"3 dB");

	// Malformed parameter: keyword is recognized but the value does
	// not parse, so valid stays false and the engine applies no preamp.
	PreampCommand bad = parsePreamp(L"loud please");
	harness.expectFalse(bad.valid, "a non-numeric preamp parameter must not parse as valid");
	harness.expectFalse(bad.noOp, "a malformed preamp parameter is not a no-op");

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
