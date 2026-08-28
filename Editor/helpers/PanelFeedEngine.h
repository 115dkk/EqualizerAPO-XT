/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 Mephistos (DCinside)

	The core of the panel preview feed, split out of PanelPreviewFeeder so
	the headless probe harness can drive it without Qt. While a plugin panel
	is open the engine runs the Editor's preview instance in one of two
	states, decided by PanelMonitorGate:

	Listen (default): capture the system mix via WASAPI loopback and run it
	through the preview instance so meters and analyzers in the plugin UI
	show live audio; the processed output is discarded. When nothing is
	playing, silence blocks keep the process calls coming, so a plugin that
	generates its own signal (a calibration noise, a test sweep) still runs
	- and is noticed.

	Render: the plugin is producing sound the system mix is not. Feed it
	silence and play its output through the default render endpoint, so
	self-calibration noise is audible without a DAW. Feeding silence while
	rendering is what breaks the feedback loop: the monitor's own playback
	re-enters the loopback capture, but a captured frame is never handed to
	the plugin while the gate is open.

	The boundary story does not change: everything here lives in the Editor
	process, and the render session is an ordinary shared-mode WASAPI
	client, exactly as if a media player were running. There is no new
	channel to the audio service - configuration still flows through the
	config file only - and an Editor crash just ends the sessions.
*/

#pragma once

#include <memory>

#include "Editor/helpers/PanelMonitorGate.h"

class VSTPluginInstance;

class PanelFeedEngine
{
public:
	struct Options
	{
		// Editor panels only process inside the VST3 editor session that
		// startEditing opens. A headless harness owns the processing
		// lifecycle itself and turns this off.
		bool requireVst3EditorSession = true;
		// Master switch for the Render half; Listen metering works without
		// it. The Editor maps EAPO_DISABLE_PANEL_MONITOR onto this.
		bool monitorEnabled = true;
	};

	PanelFeedEngine();
	~PanelFeedEngine();

	// Call before the plugin is activated: capturing prepares the instance
	// for the mix format, and VST3 setupProcessing is only legal while the
	// processor is still deactivated. Returns false when the feed could not
	// start (no endpoint, unexpected mix format); the panel then simply has
	// no live audio, as before the feed existed.
	bool start(VSTPluginInstance* effect, const Options& options);
	void stop();
	// One pump step. Call roughly every tickIntervalMs() milliseconds, and
	// always from the same thread; the Editor uses its GUI thread (see
	// PanelPreviewFeeder for why that is the contract, not a compromise).
	// Returns false when the feed shut itself down (device invalidated,
	// plugin crash) and the owner can stop ticking.
	bool tick();

	bool running() const { return state != nullptr; }
	static int tickIntervalMs();

	// Introspection for the probe harness and tests.
	PanelMonitorGate::State gateState() const;
	long long renderedFrames() const;

private:
	struct EngineState;

	// Static on purpose: they act on the passed state only, never on the
	// engine object itself.
	static bool ensureRenderStarted(EngineState& s);
	static void stopRender(EngineState& s);
	static void abandonMonitor(EngineState& s);
	bool listenTick(EngineState& s, bool processReady);
	bool renderTick(EngineState& s, bool processReady);

	std::unique_ptr<EngineState> state;
};
