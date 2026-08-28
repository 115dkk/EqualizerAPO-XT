/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The decision logic of the panel monitor: when may the preview feed play
	the plugin's processed output out loud? Only a signal the plugin created
	on its own - a calibration noise, a test sweep - may be rendered. Audio
	that merely passed through the plugin must never be, because the audio
	service is already playing the original and rendering the copy would
	double it and, worse, feed it back through the loopback capture.

	The gate separates the two cases without any knowledge of the plugin:
	self-generated audio is output that appears while the system mix has
	been silent for a while. The arm delay is deliberately long enough that
	a reverb tail decaying after playback stops cannot open the gate - a
	hall reverb with an RT60 of three seconds is below the activity
	threshold long before four seconds of input silence have accumulated.

	Closing always requires the full arm delay again. While the gate was
	open the capture heard only the monitor's own playback, so nothing can
	prove the system stayed quiet - a reopen credit would trust exactly the
	stretch nobody observed.

	Pure logic, fed with audio-time observations; kept free of Qt and
	WASAPI so EditorLogicTests can drive every transition deterministically.
*/

#pragma once

#include <algorithm>

class PanelMonitorGate
{
public:
	enum class State
	{
		// Feed captured audio (or silence) to the plugin, discard its output.
		Listen,
		// Feed silence, play the plugin's self-generated output out loud.
		Render
	};

	struct Tuning
	{
		// Input silence required before self-generated output may be played.
		int armQuietMs = 4000;
		// How long the output must stay active over an armed, silent input
		// before the gate opens - a few pump ticks, so one stray block
		// cannot start a render session. Callers advance the gate per
		// processed block (typically 10-30 ms), which is what makes this a
		// sustained-signal requirement rather than a single-peak one.
		int openActiveMs = 90;
		// Output silence that closes the gate again.
		int closeQuietMs = 1000;
		// Peak |sample| that counts as activity, about -60 dBFS.
		double activeThreshold = 1e-3;
	};

	PanelMonitorGate() {}
	explicit PanelMonitorGate(const Tuning& tuning)
		: tuning(tuning) {}

	// One observation spanning elapsedMs of processed audio time.
	// inputQuiet: every input frame of the span was silent (loopback silence
	// flag or sub-audible amplitude, or the feed substituted silence because
	// nothing was captured). outputPeak: the peak |sample| the plugin
	// produced over the span. Returns the state after the observation.
	State advance(int elapsedMs, bool inputQuiet, double outputPeak)
	{
		const bool outputActive = outputPeak >= tuning.activeThreshold;
		if (currentState == State::Listen)
		{
			// The counters saturate at their thresholds: a panel left open
			// for weeks must not walk an int into overflow.
			quietInputMs = inputQuiet
				? std::min(quietInputMs + elapsedMs, tuning.armQuietMs) : 0;
			// Output activity only counts while the input is quiet; loud
			// system audio passing through the plugin must never look
			// self-generated.
			activeOutputMs = inputQuiet && outputActive
				? std::min(activeOutputMs + elapsedMs, tuning.openActiveMs) : 0;
			if (quietInputMs >= tuning.armQuietMs && activeOutputMs >= tuning.openActiveMs)
			{
				currentState = State::Render;
				quietOutputMs = 0;
			}
		}
		else
		{
			// While rendering, the capture only hears this monitor's own
			// playback, so the input carries no information; the plugin
			// falling silent is what ends the session.
			quietOutputMs = outputActive
				? 0 : std::min(quietOutputMs + elapsedMs, tuning.closeQuietMs);
			if (quietOutputMs >= tuning.closeQuietMs)
			{
				currentState = State::Listen;
				activeOutputMs = 0;
				quietInputMs = 0;
			}
		}
		return currentState;
	}

	// Drops back to Listen with all counters cleared - for the paths where
	// the recent history no longer means anything: render device failure,
	// or a stretch in which the plugin could not process at all.
	void reset()
	{
		currentState = State::Listen;
		quietInputMs = 0;
		activeOutputMs = 0;
		quietOutputMs = 0;
	}

	State state() const { return currentState; }

private:
	Tuning tuning;
	State currentState = State::Listen;
	int quietInputMs = 0;
	int activeOutputMs = 0;
	int quietOutputMs = 0;
};
