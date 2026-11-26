/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Logging contracts for the shared BiQuad command parser and the engine factory.
*/

#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "filters/BiQuadCommand.h"
#include "filters/BiQuadFilterFactory.h"
#include "services/logging/Logging.h"
#include "../TestHarness.h"

using std::vector;
using std::wstring;

namespace
{
test::HarnessAccess harness;

class LogCapture
{
public:
	LogCapture()
	{
		if (tmpfile_s(&stream) != 0 || stream == nullptr)
			harness.fail("BiQuad trace capture opens");
		Logging::useStream(stream, true, true, false);
	}

	LogCapture(const LogCapture&) = delete;
	LogCapture& operator=(const LogCapture&) = delete;

	~LogCapture()
	{
		// HybridConvTests starts with this destination and these options.
		Logging::useStream(stdout, true, true, false);
		std::fclose(stream);
	}

	wstring contents()
	{
		std::fflush(stream);
		std::rewind(stream);
		wstring result;
		wchar_t buffer[512] = {};
		while (std::fgetws(buffer, static_cast<int>(std::size(buffer)), stream) != nullptr)
			result.append(buffer);
		return result;
	}

private:
	FILE* stream = nullptr;
};

template<typename Action>
wstring captureLog(Action action)
{
	LogCapture capture;
	action();
	return capture.contents();
}

TEST_CASE(BiQuadCommandTests, testConvenienceParseOverloadsAreSilent)
{
	const wstring addingThree = captureLog([&]() {
		wstring parameters = L"ON PK Fc 1000 Hz Gain 3 dB Q 1.5";
		BiQuadCommand command;
		harness.require(BiQuadCommand::parse(L"Filter", parameters, command),
			"three-argument parse accepts a peaking filter");
	});
	harness.expectTrue(addingThree.empty(),
		"three-argument parse does not log an Adding trace");

	const wstring addingFour = captureLog([&]() {
		wstring parameters = L"ON PK Fc 1000 Hz Gain 3 dB Q 1.5";
		BiQuadCommand command;
		wstring error;
		harness.require(BiQuadCommand::parse(L"Filter", parameters, command, error),
			"four-argument parse accepts a peaking filter");
	});
	harness.expectTrue(addingFour.empty(),
		"four-argument parse does not log an Adding trace");

	const wstring ignoringThree = captureLog([&]() {
		wstring parameters = L"ON AP Fc 100 Hz Gain 6 dB Q 0.707";
		BiQuadCommand command;
		harness.require(BiQuadCommand::parse(L"Filter", parameters, command),
			"three-argument parse accepts an all-pass with ignored gain");
	});
	harness.expectTrue(ignoringThree.empty(),
		"three-argument parse does not log an Ignoring trace");

	const wstring ignoringFour = captureLog([&]() {
		wstring parameters = L"ON AP Fc 100 Hz Gain 6 dB Q 0.707";
		BiQuadCommand command;
		wstring error;
		harness.require(BiQuadCommand::parse(L"Filter", parameters, command, error),
			"four-argument parse accepts an all-pass with ignored gain");
	});
	harness.expectTrue(ignoringFour.empty(),
		"four-argument parse does not log an Ignoring trace");
}

TEST_CASE(BiQuadCommandTests, testTraceNotesAreAppendedInLogOrder)
{
	wstring parameters = L"ON AP Fc 100 Hz Gain 6 dB Q 0.707";
	BiQuadCommand command;
	wstring error;
	vector<wstring> notes = {L"existing note"};
	harness.require(BiQuadCommand::parse(L"Filter", parameters, command, error, &notes),
		"trace-note parse accepts an all-pass with ignored gain");
	harness.requireEqual(notes.size(), (size_t)3, "the parser appends two trace notes");
	harness.expectTrue(notes[0] == L"existing note", "existing trace notes are retained");
	harness.expectTrue(notes[1] == L"Ignoring gain for filter of type all-pass",
		"the Ignoring note is first");
	harness.expectTrue(notes[2] == L"Adding all-pass filter with frequency 100 Hz and Q 0.707",
		"the Adding note follows it with the original text");
}

TEST_CASE(BiQuadCommandTests, testFactoryWritesCollectedTraceNotes)
{
	LineOutcome outcome;
	const wstring log = captureLog([&]() {
		BiQuadFilterFactory factory;
		wstring command = L"Filter";
		wstring parameters = L"ON AP Fc 100 Hz Gain 6 dB Q 0.707";
		outcome = factory.createFilter(L"BiQuadCommandTests.txt", command, parameters);
	});

	harness.expectTrue(outcome.kind == LineOutcome::Kind::Produced && outcome.filters.size() == 1,
		"the factory produces the all-pass filter");
	const wstring expected =
		L"(TRACE) Ignoring gain for filter of type all-pass\n"
		L"(TRACE) Adding all-pass filter with frequency 100 Hz and Q 0.707\n";
	harness.expectTrue(log == expected,
		"the factory writes the parser notes with the original text and order");
}
}
