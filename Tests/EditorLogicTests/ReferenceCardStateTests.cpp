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
