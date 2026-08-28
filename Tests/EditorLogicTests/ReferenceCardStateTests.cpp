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

void testReferenceCardDerivesSharedPresentationState()
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
