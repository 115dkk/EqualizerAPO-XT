/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include "Editor/helpers/PanelMonitorGate.h"

namespace
{
using State = PanelMonitorGate::State;

// The gate advances on audio-time observations; 30 ms is the engine's pump
// cadence, and the loops below spell durations in ticks of it.
constexpr int tickMs = 30;

void advanceFor(PanelMonitorGate& gate, int milliseconds, bool inputQuiet, double outputPeak)
{
	for (int elapsed = 0; elapsed < milliseconds; elapsed += tickMs)
		gate.advance(tickMs, inputQuiet, outputPeak);
}
}

void testPanelMonitorGateOpensOnlyForSelfGeneratedAudio()
{
	const PanelMonitorGate::Tuning tuning;
	PanelMonitorGate gate;

	// Loud audio passing through the plugin - the system is playing music -
	// never opens the gate, no matter how long it goes on.
	advanceFor(gate, tuning.armQuietMs * 3, /*inputQuiet=*/ false, /*outputPeak=*/ 0.8);
	expectTrue(gate.state() == State::Listen,
		QStringLiteral("pass-through audio never opens the gate"));

	// A quiet system with a silent plugin stays in Listen too.
	advanceFor(gate, tuning.armQuietMs * 2, true, 0.0);
	expectTrue(gate.state() == State::Listen,
		QStringLiteral("silence on both sides keeps the gate closed"));

	// The plugin starts generating: the input has been quiet far past the
	// arm delay, so a brief burst of output activity opens the gate.
	advanceFor(gate, tuning.openActiveMs + tickMs, true, 0.5);
	expectTrue(gate.state() == State::Render,
		QStringLiteral("self-generated output on an armed gate opens it"));
}

void testPanelMonitorGateArmDelayOutlivesReverbTails()
{
	const PanelMonitorGate::Tuning tuning;
	PanelMonitorGate gate;

	// Music plays, then stops; a reverb tail rings on well above the
	// activity threshold, but decays below it before the arm delay elapses
	// - the RT60 argument in the header. The gate must ride through.
	advanceFor(gate, 2000, false, 0.8);
	advanceFor(gate, tuning.armQuietMs / 2, true, 0.1);
	advanceFor(gate, tuning.armQuietMs, true, 0.0);
	expectTrue(gate.state() == State::Listen,
		QStringLiteral("a decaying tail under the arm delay does not open the gate"));

	// Had the tail still been loud when the arm delay expired, that would be
	// a generator by definition - held output over a long-silent input.
	PanelMonitorGate drone;
	advanceFor(drone, tuning.armQuietMs + tuning.openActiveMs + tickMs, true, 0.1);
	expectTrue(drone.state() == State::Render,
		QStringLiteral("output still active past the arm delay counts as self-generated"));
}

void testPanelMonitorGateClosesAndRearms()
{
	const PanelMonitorGate::Tuning tuning;
	PanelMonitorGate gate;
	advanceFor(gate, tuning.armQuietMs + tuning.openActiveMs + tickMs, true, 0.5);
	requireTrue(gate.state() == State::Render, QStringLiteral("fixture gate opened"));

	// While the plugin keeps generating, the gate stays open.
	advanceFor(gate, tuning.closeQuietMs * 2, true, 0.5);
	expectTrue(gate.state() == State::Render,
		QStringLiteral("an active generator holds the gate open"));

	// The plugin falls silent; after the close hold the gate drops back.
	advanceFor(gate, tuning.closeQuietMs + tickMs, true, 0.0);
	expectTrue(gate.state() == State::Listen,
		QStringLiteral("a quiet generator closes the gate"));

	// A close demands the full arm delay again: while the gate was open the
	// capture heard only the monitor itself, so nothing proved the system
	// stayed quiet, and no reopen credit may survive.
	advanceFor(gate, tuning.armQuietMs / 4, true, 0.5);
	expectTrue(gate.state() == State::Listen,
		QStringLiteral("a fresh generator burst right after a close stays unplayed"));
	advanceFor(gate, tuning.armQuietMs + tickMs, true, 0.5);
	expectTrue(gate.state() == State::Render,
		QStringLiteral("the fully re-armed gate opens again"));

	// reset() is the error path: everything starts over there too.
	gate.reset();
	expectTrue(gate.state() == State::Listen, QStringLiteral("reset returns to Listen"));
	advanceFor(gate, tuning.openActiveMs * 4, true, 0.5);
	expectTrue(gate.state() == State::Listen,
		QStringLiteral("after reset the full arm delay applies again"));
}

void testPanelMonitorGateInterruptedArmStartsOver()
{
	const PanelMonitorGate::Tuning tuning;
	PanelMonitorGate gate;

	// The system goes quiet, almost arms, then briefly plays again: the
	// quiet counter starts over, so generated output right afterwards stays
	// unplayed until a full arm delay has passed.
	advanceFor(gate, tuning.armQuietMs - tickMs * 2, true, 0.0);
	gate.advance(tickMs, false, 0.0);
	advanceFor(gate, tuning.openActiveMs * 4, true, 0.5);
	expectTrue(gate.state() == State::Listen,
		QStringLiteral("input activity restarts the arm delay"));
}
