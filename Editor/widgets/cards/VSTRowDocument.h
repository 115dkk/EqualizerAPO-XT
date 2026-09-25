/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The document state of one VSTPlugin row, shared by the modern card
	(VSTCardEditor) and the legacy row (VSTPluginFilterGUI): the main-bus
	contract (VSTBusModel) and the per-slot channel fill (VSTSlotFillModel)
	against the channels selected at the row. Both widgets used to keep
	their own copy of this state and of the rule that ties the two together;
	the copies had already drifted (audit #348 B1). This is now the only
	place the rule "changing a side's layout clears that side's fill" lives.

	No Qt on purpose: EditorLogicTests pins the rules directly.
*/

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vst/VST3BusLayout.h"
#include "VSTBusModel.h"
#include "VSTSlotFillModel.h"

class VSTRowDocument
{
public:
	// legacyStereoInput migrates a "StereoInput 1" line into the equivalent
	// contract (VSTBusModel); pass false to keep the flag out of the
	// document. The fill lists belong to the contract's layouts.
	VSTRowDocument(const std::optional<VST3BusContract>& contract, bool legacyStereoInput,
		std::vector<std::wstring> inputFill, std::vector<std::wstring> outputFill);

	const VSTBusModel& bus() const noexcept;
	// Always carries the bus contract and the selection; read the fill lists
	// (inputFill/outputFill) from here when serializing.
	const VSTSlotFillModel& fill() const noexcept;

	// Sets both layouts; a side whose layout changed loses its fill list,
	// because its slot count no longer matches and the stale list would not
	// parse.
	void setLayouts(VST3BusLayout input, VST3BusLayout output);
	// Removes the contract and both fill lists.
	void clearLayouts();
	// The channels selected at this row (the channel flow).
	void setSelectedChannels(std::vector<std::wstring> names);
	// Assigns a channel (or L"-") to a slot; see VSTSlotFillModel::pickSlot.
	void pickSlot(bool output, int slot, const std::wstring& value);
	// Both sides back to the implicit default: the fill lists leave the line.
	void clearFill();

private:
	VSTBusModel busModel;
	VSTSlotFillModel fillModel;
};
