/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/cards/ReferenceCardView.h"
#include "Editor/widgets/cards/ReferencePresence.h"
#include "Editor/widgets/cards/ReferenceProbeRequests.h"

// A card asks about its file off the UI thread (audit #467 TD-59); these pin
// which answers still reach it.
TEST_CASE(EditorLogicTests, testReferenceProbeDropsAStaleAnswer)
{
	ReferenceProbeRequests requests;
	expectTrue(requests.begin(QStringLiteral("a.wav")), "the first question starts a probe");
	expectTrue(requests.begin(QStringLiteral("b.wav")), "the user typed on: a new path starts its own probe");
	expectFalse(requests.finish(QStringLiteral("a.wav")), "the answer for the path no longer shown is dropped");
	expectTrue(requests.finish(QStringLiteral("b.wav")), "the answer for the shown path is delivered");

	expectTrue(requests.begin(QStringLiteral("c.wav")), "a third path starts a probe");
	requests.clear();
	expectFalse(requests.finish(QStringLiteral("c.wav")),
		"once the card asks about nothing (the file went missing), a late answer is dropped");
}

TEST_CASE(EditorLogicTests, testReferenceProbeDeliversTheCurrentAnswerOnce)
{
	ReferenceProbeRequests requests;
	expectTrue(requests.begin(QStringLiteral("a.wav")), "the question starts a probe");
	expectTrue(requests.finish(QStringLiteral("a.wav")), "its answer is delivered");
	expectFalse(requests.finish(QStringLiteral("a.wav")), "and only once");
	expectTrue(requests.begin(QStringLiteral("a.wav")), "asking again after the answer probes again");
}

TEST_CASE(EditorLogicTests, testReferenceProbeCoalescesARepeatedQuestion)
{
	ReferenceProbeRequests requests;
	expectTrue(requests.begin(QStringLiteral("a.wav")), "the question starts a probe");
	expectFalse(requests.begin(QStringLiteral("a.wav")), "the same path asked again while in flight is not probed twice");
	expectTrue(requests.begin(QStringLiteral("b.wav")), "another path starts its probe");
	expectFalse(requests.begin(QStringLiteral("a.wav")), "returning to a path still in flight waits for that probe");
	expectFalse(requests.finish(QStringLiteral("b.wav")), "the path left behind is stale");
	expectTrue(requests.finish(QStringLiteral("a.wav")), "the coalesced probe answers the current request");
}

// Whether the file exists is asked off the UI thread too (audit #467 TD-59);
// these pin what the card knows meanwhile and which answers it keeps.
TEST_CASE(EditorLogicTests, testReferencePresenceIsUnknownUntilAnswered)
{
	ReferencePresence presence;
	expectTrue(presence.answerFor(QStringLiteral("a.wav")) == ReferencePresence::Answer::Unknown,
		"nothing is known before the card asks");
	expectTrue(presence.begin(QStringLiteral("a.wav")), "the first question starts a probe");
	expectTrue(presence.answerFor(QStringLiteral("a.wav")) == ReferencePresence::Answer::Unknown,
		"while the probe runs the card is still checking");
	expectTrue(presence.finish(QStringLiteral("a.wav"), true), "the answer for the shown path is kept");
	expectTrue(presence.answerFor(QStringLiteral("a.wav")) == ReferencePresence::Answer::Present,
		"the kept answer says the file exists");
	expectFalse(presence.begin(QStringLiteral("a.wav")), "a known answer needs no second probe");

	ReferencePresence absent;
	absent.begin(QStringLiteral("b.wav"));
	expectTrue(absent.finish(QStringLiteral("b.wav"), false), "a missing answer is kept as well");
	expectTrue(absent.answerFor(QStringLiteral("b.wav")) == ReferencePresence::Answer::Missing,
		"the kept answer says the file is missing");
}

TEST_CASE(EditorLogicTests, testReferencePresenceDropsAnAnswerForAReplacedPath)
{
	ReferencePresence presence;
	expectTrue(presence.begin(QStringLiteral("a.wav")), "the first path starts a probe");
	expectTrue(presence.begin(QStringLiteral("b.wav")), "the user typed on: the new path starts its own probe");
	expectFalse(presence.finish(QStringLiteral("a.wav"), true), "the answer for the replaced path is dropped");
	expectTrue(presence.answerFor(QStringLiteral("a.wav")) == ReferencePresence::Answer::Unknown,
		"a dropped answer is not remembered");
	expectTrue(presence.answerFor(QStringLiteral("b.wav")) == ReferencePresence::Answer::Unknown,
		"nor does it answer for the shown path");
	expectTrue(presence.finish(QStringLiteral("b.wav"), false), "the shown path's answer is kept");
	expectTrue(presence.answerFor(QStringLiteral("b.wav")) == ReferencePresence::Answer::Missing,
		"and it is the shown path's answer");
}

TEST_CASE(EditorLogicTests, testReferencePresenceCoalescesARepeatedQuestion)
{
	ReferencePresence presence;
	expectTrue(presence.begin(QStringLiteral("a.wav")), "the question starts a probe");
	expectFalse(presence.begin(QStringLiteral("a.wav")), "asked again while it runs, no second probe starts");
	expectTrue(presence.finish(QStringLiteral("a.wav"), true), "the one probe answers the repeated question");
	expectFalse(presence.finish(QStringLiteral("a.wav"), true), "and answers it only once");
}

TEST_CASE(EditorLogicTests, testReferencePresenceForgetsWhenThePathChanges)
{
	ReferencePresence presence;
	presence.begin(QStringLiteral("a.wav"));
	presence.finish(QStringLiteral("a.wav"), true);
	expectTrue(presence.begin(QStringLiteral("b.wav")), "another path is unknown and probed");
	expectTrue(presence.begin(QStringLiteral("a.wav")), "back on the first path, its old answer is gone");
	expectTrue(presence.answerFor(QStringLiteral("a.wav")) == ReferencePresence::Answer::Unknown,
		"the card checks the first path again");

	ReferencePresence chosen;
	chosen.begin(QStringLiteral("c.wav"));
	chosen.finish(QStringLiteral("c.wav"), false);
	chosen.forget();
	expectTrue(chosen.answerFor(QStringLiteral("c.wav")) == ReferencePresence::Answer::Unknown,
		"the user chose the same path again: the old answer is forgotten");
	expectTrue(chosen.begin(QStringLiteral("c.wav")), "and the path is probed again");
}

TEST_CASE(EditorLogicTests, testReferencePresenceClearDropsARunningAnswer)
{
	ReferencePresence presence;
	expectTrue(presence.begin(QStringLiteral("a.wav")), "the question starts a probe");
	presence.clear();
	expectFalse(presence.finish(QStringLiteral("a.wav"), true),
		"once the card asks about nothing, the late answer is dropped");
	expectTrue(presence.answerFor(QStringLiteral("a.wav")) == ReferencePresence::Answer::Unknown,
		"and nothing is remembered from it");

	// A card that knows its file is missing asks the audio service nothing
	// and says so (askNothing); that must not send it back to checking.
	ReferencePresence known;
	known.begin(QStringLiteral("b.wav"));
	known.finish(QStringLiteral("b.wav"), false);
	known.clear();
	expectTrue(known.answerFor(QStringLiteral("b.wav")) == ReferencePresence::Answer::Missing,
		"clear keeps the answer already known for the shown path");
}

TEST_CASE(EditorLogicTests, testReferenceCardDerivesSharedPresentationState)
{
	ReferenceCardState state;
	state.missing = true;
	state.editText = QStringLiteral("  impulses\\room.wav  ");
	state.statusSeverity = ReferenceCardState::Severity::Critical;

	expectTrue(referenceCardNeedsLocate(state), QStringLiteral("a written missing reference needs Locate"));
	expectEqual(
		referenceCardSeverityName(state.statusSeverity),
		QStringLiteral("critical"),
		QStringLiteral("critical severity has one shared property token"));

	state.editText = QStringLiteral("   ");
	expectFalse(referenceCardNeedsLocate(state), QStringLiteral("an empty reference is unconfigured, not locatable"));
	expectEqual(
		referenceCardSeverityName(ReferenceCardState::Severity::None),
		QStringLiteral("none"),
		QStringLiteral("quiet state has one shared property token"));
}
