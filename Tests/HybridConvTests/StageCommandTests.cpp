/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Round-trip tests for the shared "Stage:" config-line codec
	(filters/StageCommand.{h,cpp}), which the engine factory and the
	Editor GUI both consume.
*/

#include <string>
#include <vector>

#include "filters/StageCommand.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("StageCommandTests");

StageCommand parseStages(const wstring& parameters)
{
	StageCommand cmd;
	if (!StageCommand::parse(L"Stage", parameters, cmd))
		harness.fail("'Stage' command was not recognized");
	return cmd;
}

void testTokenization()
{
	StageCommand cmd = parseStages(L" pre-mix post-mix ");
	harness.requireEqual(cmd.stages.size(), (size_t)2, "'pre-mix post-mix' selector count");
	harness.expectTrue(cmd.stages[0] == StageCommand::preMix, "first selector");
	harness.expectTrue(cmd.stages[1] == StageCommand::postMix, "second selector");

	// Selectors are case-insensitive; the codec lower-cases like the factory.
	cmd = parseStages(L"Capture PRE-MIX");
	harness.requireEqual(cmd.stages.size(), (size_t)2, "mixed-case selector count");
	harness.expectTrue(cmd.stages[0] == StageCommand::capture, "upper-case selector is lower-cased");
	harness.expectTrue(cmd.stages[1] == StageCommand::preMix, "all-caps selector is lower-cased");

	// Consecutive spaces do not produce empty selectors.
	cmd = parseStages(L"pre-mix   capture");
	harness.expectEqual(cmd.stages.size(), (size_t)2, "double-space selector count");

	// Unknown selectors are kept so the factory can report them.
	cmd = parseStages(L"premix");
	harness.expectEqual(cmd.stages.size(), (size_t)1, "unknown selector is kept");
	harness.expectFalse(cmd.contains(StageCommand::preMix), "unknown selector does not match pre-mix");

	// An empty selector list is a valid Stage line that matches no stage.
	cmd = parseStages(L"   ");
	harness.expectEqual(cmd.stages.size(), (size_t)0, "blank parameters give no selectors");
}

void testContains()
{
	StageCommand cmd = parseStages(L"post-mix capture");
	harness.expectTrue(cmd.contains(StageCommand::postMix), "contains post-mix");
	harness.expectTrue(cmd.contains(StageCommand::capture), "contains capture");
	harness.expectFalse(cmd.contains(StageCommand::preMix), "does not contain pre-mix");
}

void testCommandRecognition()
{
	StageCommand cmd;
	harness.expectFalse(StageCommand::parse(L"Preamp", L"pre-mix", cmd), "'Preamp' must not parse as Stage");
	harness.expectFalse(StageCommand::parse(L"stage", L"pre-mix", cmd), "command match is case-sensitive");
}

void testRoundTrip()
{
	const wstring cases[] = {
		L"pre-mix",
		L"pre-mix post-mix capture",
		L"  Capture   PRE-MIX ",
		L"",
	};

	for (const wstring& parameters : cases)
	{
		StageCommand first;
		StageCommand::parse(L"Stage", parameters, first);
		wstring serialized = first.serialize();

		StageCommand second;
		StageCommand::parse(L"Stage", serialized, second);
		harness.expectTrue(first.stages == second.stages, "serialize/parse round trip is stable");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}
}

void runStageCommandTests()
{
	testTokenization();
	testContains();
	testCommandRecognition();
	testRoundTrip();

	harness.report();
}
