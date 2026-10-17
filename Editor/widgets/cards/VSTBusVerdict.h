/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The modern VST card's bus semantics as a decision table (audit #422 D4):
	when the bus strip shows, when its selectors may act, what the compact
	verdict says and which long-form message the card's status line carries.
	The card gathers the facts (the saved contract, the loaded module and a
	live negotiation on its own instance) and applies the verdict to the
	strip; the table itself has no widgets, so EditorLogicTests covers every
	row. The VST2 and stale-keys rows were field bugs (issue #216).
*/

#pragma once

#include <optional>

#include <QString>

#include "Editor/widgets/cards/ReferenceCardView.h"
#include "vst/VST3BusLayout.h"

struct VSTBusFacts
{
	// The line carries Input/Output layouts.
	bool hasContract = false;
	VST3BusLayout requestedInput = VST3BusLayout::Auto;
	VST3BusLayout requestedOutput = VST3BusLayout::Auto;
	// The legacy StereoInput flag was read as Input Stereo / Output Auto.
	bool migratedLegacyStereoInput = false;
	bool instanceLoaded = false;
	// The ABI the loader established; read only while instanceLoaded (the
	// file extension is not format evidence).
	bool isVST3 = false;
	bool embedded = false;
	// The negotiation on the editor's own instance, made only when
	// negotiates(): the same call the engine makes.
	bool accepted = false;
	// What engaged, read only for an accepted request with an Auto
	// direction: the negotiated arrangement (nullopt when it is outside the
	// config grammar) and the instance's channel count for each side.
	std::optional<VST3BusLayout> negotiatedInput;
	std::optional<VST3BusLayout> negotiatedOutput;
	int negotiatedInputChannels = 0;
	int negotiatedOutputChannels = 0;

	// The selectors may act and the verdict comes from a live negotiation.
	bool negotiates() const;
	bool anyAuto() const;
};

struct VSTBusVerdict
{
	enum class Tone
	{
		Neutral,
		Success,
		Warning,
		Critical
	};
	enum class Readout
	{
		// Leave the last verdict on display.
		Keep,
		// One short word (possibly empty: lamp only).
		Single,
		// The negotiated input and output, which the skin joins.
		Pair
	};

	// Without a loaded plug-in and a saved contract there is nothing to show
	// or protect; the selectors and the readout are then left as they are.
	bool stripVisible = false;
	bool selectorsEnabled = false;
	// Why the selectors are locked; empty while they are enabled.
	QString selectorsDisabledReason;
	Readout readout = Readout::Keep;
	QString verdictText;
	QString verdictInput;
	QString verdictOutput;
	Tone tone = Tone::Neutral;
	// The long-form message for the card's status line; empty for none.
	QString statusText;
	ReferenceCardState::Severity statusSeverity = ReferenceCardState::Severity::None;

	static VSTBusVerdict describe(const VSTBusFacts& facts);
};
