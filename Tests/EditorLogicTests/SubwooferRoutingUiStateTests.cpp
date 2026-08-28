/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/widgets/subwooferrouting/SubwooferRoutingUiState.h"
#include "SubwooferRouting/Preset.h"

#include <QString>

namespace
{
SubwooferRoutingUiState makeFixture(unsigned sampleRate)
{
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(subroute::kIssue246FrontRear41PresetId);
	requireTrue(preset.succeeded(), "subwoofer-routing UI state fixture created");
	return SubwooferRoutingUiState(*preset.state, sampleRate);
}
}

// Audit #275 B3/TD-30: this mutation/validation layer is exactly what the
// widget-free test seam targets, but it lived behind Q_OBJECT (which this
// binary cannot moc) and had zero tests while being the largest UI model in
// the tree. The signal-free core is now pinned here.
void testSubwooferRoutingUiStateTracksMutationsAndValidation()
{
	SubwooferRoutingUiState state = makeFixture(48000);

	expectFalse(state.isDirty(), QStringLiteral("a freshly loaded state is clean"));
	expectTrue(state.validation().succeeded(),
		QStringLiteral("the built-in preset validates"));
	expectTrue(state.computedTrimDb().has_value(),
		QStringLiteral("compiling with a sample rate yields a headroom trim readout"));

	expectTrue(state.setSourceLfeGainDb(-3.0),
		QStringLiteral("changing the source LFE gain reports a mutation"));
	expectTrue(state.isDirty(), QStringLiteral("a committed mutation marks the state dirty"));

	bool gainApplied = false;
	for (const subroute::Path& path : state.state().paths)
	{
		if (path.kind == subroute::PathKind::SourceLfe && path.preGainDb == -3.0)
			gainApplied = true;
	}
	expectTrue(gainApplied, QStringLiteral("the gain reaches the source LFE path"));
}

void testSubwooferRoutingUiStateRejectsUnknownTargets()
{
	SubwooferRoutingUiState state = makeFixture(48000);

	expectFalse(state.setGroupHighPass("no-such-group", 120.0),
		QStringLiteral("an unknown group mutates nothing"));
	expectFalse(state.setBassPathLowPass("no-such-path", 90.0),
		QStringLiteral("an unknown bass path mutates nothing"));
	expectFalse(state.setPathPolarity("no-such-path", true),
		QStringLiteral("an unknown path's polarity mutates nothing"));
	expectFalse(state.isDirty(),
		QStringLiteral("rejected mutations leave the state clean - the QObject wrapper emits nothing for them"));
}

void testSubwooferRoutingUiStateHeadroomModes()
{
	SubwooferRoutingUiState state = makeFixture(48000);

	expectTrue(state.setHeadroomAuto(false),
		QStringLiteral("switching to manual headroom is a mutation"));
	expectTrue(state.setManualTrimDb(-6.0),
		QStringLiteral("setting a manual trim is a mutation"));
	expectTrue(state.state().headroom.mode == subroute::HeadroomMode::Manual,
		QStringLiteral("the headroom mode reaches the state"));
	expectTrue(state.state().headroom.manualTrimDb == -6.0,
		QStringLiteral("the manual trim reaches the state"));

	// Without a sample rate the state cannot compile, so validation falls
	// back to the structural check and no trim readout exists.
	SubwooferRoutingUiState uncompiled = makeFixture(0);
	expectFalse(uncompiled.computedTrimDb().has_value(),
		QStringLiteral("no sample rate means no computed trim"));
	expectTrue(uncompiled.validation().succeeded(),
		QStringLiteral("structural validation still runs without a sample rate"));
}
