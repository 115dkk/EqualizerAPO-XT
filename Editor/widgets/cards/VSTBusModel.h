/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Document-side state of the modern VST card's main-bus contract. Keeping
	the legacy StereoInput migration and the paired Input/Output values out of
	widget code makes the config transition deterministic and testable in
	EditorLogicTests without a QWidget in sight.
*/

#pragma once

#include <optional>

#include "vst/VST3BusLayout.h"

class VSTBusModel
{
public:
	// A hand-edited line can carry both generations of settings; the explicit
	// Input/Output pair is authoritative. Otherwise a legacy "StereoInput 1"
	// becomes the equivalent asymmetric contract (Input Stereo, Output Auto)
	// and the obsolete flag is never emitted again by this editor.
	explicit VSTBusModel(const std::optional<VST3BusContract>& contract = std::nullopt,
		bool legacyStereoInput = false);

	// The current values, Auto while no contract is saved.
	VST3BusLayout input() const noexcept;
	VST3BusLayout output() const noexcept;
	const std::optional<VST3BusContract>& contract() const noexcept;
	// True while the contract only exists because a legacy StereoInput flag
	// was migrated; the card surfaces this once so the rewrite is not silent.
	bool migratedLegacyStereoInput() const noexcept;

	void setLayouts(VST3BusLayout input, VST3BusLayout output);
	// Drops the contract entirely (the repair affordance for stale layout
	// keys on a loaded VST2 module, and the way back to full Auto).
	void clear();

private:
	std::optional<VST3BusContract> currentContract;
	bool migratedLegacy = false;
};
