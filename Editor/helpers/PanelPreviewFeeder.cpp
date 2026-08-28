/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 Mephistos (DCinside)
*/

#include "PanelPreviewFeeder.h"

PanelPreviewFeeder::PanelPreviewFeeder()
{
	pumpTimer.setInterval(PanelFeedEngine::tickIntervalMs());
	connect(&pumpTimer, &QTimer::timeout, this, &PanelPreviewFeeder::pump);
}

PanelPreviewFeeder::~PanelPreviewFeeder()
{
	stop();
}

void PanelPreviewFeeder::start(VSTPluginInstance* effect)
{
	stop();

	// Field kill switches, doubling as the control arms of the A/B proofs:
	// EAPO_DISABLE_PANEL_FEED restores the pre-feed Editor (dead meters, no
	// monitor), EAPO_DISABLE_PANEL_MONITOR keeps the meters but never plays
	// self-generated plugin audio.
	if (qEnvironmentVariableIsSet("EAPO_DISABLE_PANEL_FEED"))
		return;

	PanelFeedEngine::Options options;
	options.requireVst3EditorSession = true;
	options.monitorEnabled = !qEnvironmentVariableIsSet("EAPO_DISABLE_PANEL_MONITOR");

	if (engine.start(effect, options))
		pumpTimer.start();
}

void PanelPreviewFeeder::stop()
{
	pumpTimer.stop();
	engine.stop();
}

// Deliberately on the GUI thread; PanelFeedEngine::tick documents why that
// is the serialization contract, not a compromise.
void PanelPreviewFeeder::pump()
{
	if (!engine.tick())
		pumpTimer.stop();
}
