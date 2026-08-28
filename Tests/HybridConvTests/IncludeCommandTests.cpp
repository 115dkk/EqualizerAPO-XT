/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared "Include:" config-line codec
	(filters/IncludeCommand.{h,cpp}), which the engine factory and the
	Editor GUI both consume.
*/

#include <string>

#include "filters/IncludeCommand.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("IncludeCommandTests");

wstring parsePath(const wstring& parameters)
{
	IncludeCommand cmd;
	if (!IncludeCommand::parse(L"Include", parameters, cmd))
		harness.fail("'Include' command was not recognized");
	return cmd.path;
}

void testPathExtraction()
{
	harness.expectTrue(parsePath(L"  config.txt") == L"config.txt", "leading whitespace is stripped");
	harness.expectTrue(parsePath(L"my config.txt") == L"my config.txt", "embedded spaces are part of the path");
	harness.expectTrue(parsePath(L"..\\other\\config.txt") == L"..\\other\\config.txt", "relative path is kept verbatim");
	harness.expectTrue(parsePath(L"C:\\cfg\\a.txt") == L"C:\\cfg\\a.txt", "absolute path is kept verbatim");

	// Trailing characters belong to the path as written; the engine passes
	// them to the file system unchanged.
	harness.expectTrue(parsePath(L" config.txt ") == L"config.txt ", "trailing whitespace is kept");

	harness.expectTrue(parsePath(L"   ") == L"", "blank parameters give an empty path");
}

void testCommandRecognition()
{
	IncludeCommand cmd;
	harness.expectFalse(IncludeCommand::parse(L"Preamp", L"config.txt", cmd), "'Preamp' must not parse as Include");
	harness.expectFalse(IncludeCommand::parse(L"include", L"config.txt", cmd), "command match is case-sensitive");
}

void testRoundTrip()
{
	const wstring cases[] = {
		L"config.txt",
		L"  sub dir\\my config.txt",
		L"C:\\EqualizerAPO\\config\\extra.txt",
		L"",
	};

	for (const wstring& parameters : cases)
	{
		IncludeCommand first;
		IncludeCommand::parse(L"Include", parameters, first);
		wstring serialized = first.serialize();

		IncludeCommand second;
		IncludeCommand::parse(L"Include", serialized, second);
		harness.expectTrue(first.path == second.path, "serialize/parse round trip is stable");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}
}

void runIncludeCommandTests()
{
	testPathExtraction();
	testCommandRecognition();
	testRoundTrip();

	harness.report();
}
