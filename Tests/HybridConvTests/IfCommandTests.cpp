/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared If/ElseIf/Else/EndIf config-line codec
	(filters/IfCommand.{h,cpp}), which the engine factory consumes.
*/

#include <string>

#include "filters/IfCommand.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("IfCommandTests");

void testCommandRecognition()
{
	IfCommand cmd;

	harness.expectTrue(IfCommand::parse(L"If", L"x > 1", cmd), "'If' is recognized");
	harness.expectTrue(cmd.kind == IfCommand::Kind::If, "'If' kind");
	harness.expectTrue(cmd.expression == L"x > 1", "If expression is trimmed text");

	harness.expectTrue(IfCommand::parse(L"ElseIf", L"  y == 2  ", cmd), "'ElseIf' is recognized");
	harness.expectTrue(cmd.kind == IfCommand::Kind::ElseIf, "'ElseIf' kind");
	harness.expectTrue(cmd.expression == L"y == 2", "ElseIf expression is trimmed");

	harness.expectTrue(IfCommand::parse(L"Else", L"", cmd), "'Else' is recognized");
	harness.expectTrue(cmd.kind == IfCommand::Kind::Else, "'Else' kind");
	harness.expectTrue(cmd.expression == L"", "Else has no expression");

	harness.expectTrue(IfCommand::parse(L"EndIf", L"", cmd), "'EndIf' is recognized");
	harness.expectTrue(cmd.kind == IfCommand::Kind::EndIf, "'EndIf' kind");

	harness.expectFalse(IfCommand::parse(L"Preamp", L"x", cmd), "'Preamp' must not parse as If");
	harness.expectFalse(IfCommand::parse(L"if", L"x", cmd), "command match is case-sensitive");
	harness.expectFalse(IfCommand::parse(L"Endif", L"", cmd), "'Endif' casing must not parse");
}

void testRoundTrip()
{
	const wstring commands[] = {L"If", L"ElseIf", L"Else", L"EndIf"};
	const wstring cases[] = {
		L"x > 1",
		L"  readRegString(`a`) == \"b\"  ",
		L"",
	};

	for (const wstring& command : commands)
	{
		for (const wstring& parameters : cases)
		{
			IfCommand first;
			IfCommand::parse(command, parameters, first);
			wstring serialized = first.serialize();

			IfCommand second;
			IfCommand::parse(command, serialized, second);
			harness.expectTrue(first.kind == second.kind, "round trip keeps the kind");
			harness.expectTrue(first.expression == second.expression, "serialize/parse round trip is stable");
			harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
		}
	}
}
}

void runIfCommandTests()
{
	testCommandRecognition();
	testRoundTrip();

	harness.report();
}
