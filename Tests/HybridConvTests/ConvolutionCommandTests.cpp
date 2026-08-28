/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared "Convolution:" config-line codec
	(filters/ConvolutionCommand.{h,cpp}), which the engine factory and the
	Editor GUI both consume.
*/

#include <string>

#include "filters/ConvolutionCommand.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("ConvolutionCommandTests");

wstring parsePath(const wstring& parameters)
{
	ConvolutionCommand cmd;
	if (!ConvolutionCommand::parse(L"Convolution", parameters, cmd))
		harness.fail("'Convolution' command was not recognized");
	return cmd.path;
}

void testParse()
{
	harness.expectTrue(parsePath(L" ir.wav") == L"ir.wav", "leading whitespace is trimmed");
	harness.expectTrue(parsePath(L"sub dir\\room ir.wav ") == L"sub dir\\room ir.wav",
		"inner spaces survive, trailing whitespace is trimmed");
	harness.expectTrue(parsePath(L"C:\\IRs\\room.wav") == L"C:\\IRs\\room.wav", "absolute path");

	// Quotes and environment variables are preserved for the resolver.
	harness.expectTrue(parsePath(L"\"quoted path.wav\"") == L"\"quoted path.wav\"", "quotes preserved");
	harness.expectTrue(parsePath(L"%USERPROFILE%\\ir.wav") == L"%USERPROFILE%\\ir.wav",
		"environment variables preserved");

	// Empty path is recognized; emptiness policy stays with the caller.
	harness.expectTrue(parsePath(L"   ") == L"", "blank parameters give an empty path");

	ConvolutionCommand cmd;
	harness.expectFalse(ConvolutionCommand::parse(L"Channel", L"ir.wav", cmd),
		"'Channel' must not parse as Convolution");
}

void testRoundTrip()
{
	const wstring cases[] = {
		L"ir.wav",
		L" sub\\room ir.wav ",
		L"\"quoted.wav\"",
		L"%USERPROFILE%\\ir.wav",
	};

	for (const wstring& parameters : cases)
	{
		ConvolutionCommand first;
		ConvolutionCommand::parse(L"Convolution", parameters, first);
		wstring serialized = first.serialize();

		ConvolutionCommand second;
		ConvolutionCommand::parse(L"Convolution", serialized, second);
		harness.expectTrue(second.path == first.path, "serialize/parse round trip is stable");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}
}

void runConvolutionCommandTests()
{
	testParse();
	testRoundTrip();

	harness.report();
}
