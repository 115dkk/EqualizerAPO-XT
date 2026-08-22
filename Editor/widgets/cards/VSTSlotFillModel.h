/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Document-side state of the per-slot channel fill on a VST card: the two
	InputChannels/OutputChannels lists against the contract's layouts, and
	the channels selected at this row (Channel:/Copy: flow). Keeping the
	rail/latch presence rules, the materialize-on-first-edit behavior and
	the out-of-selection detection out of widget code makes them testable
	in EditorLogicTests without a QWidget.
*/

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vst/VST3BusLayout.h"

class VSTSlotFillModel
{
public:
	// The contract the fills belong to. A side with an Auto layout (or no
	// contract at all) has no rail; the caller keeps the fill lists in sync
	// with layout changes (they clear when a side's layout changes).
	void setContract(const std::optional<VST3BusContract>& contract);
	void setFill(std::vector<std::wstring> input, std::vector<std::wstring> output);
	// The channels selected at this row, in selection order. Slot defaults
	// and the pick menu follow this list, so Channel: restrictions and
	// Copy-created channels reach the fill exactly as the engine sees them.
	void setSelectedChannels(std::vector<std::wstring> names);

	// A rail exists only for an explicitly negotiated side.
	bool railPresent(bool output) const;
	// The fold latch exists only when both rails do: with a single rail
	// there is nothing worth collapsing.
	bool latchPresent() const;

	int slotCount(bool output) const;
	// The layout's slot role in channel order ("L", "R", "C", ...).
	std::wstring slotRole(bool output, int slot) const;
	// The effective value shown in a slot: the explicit list entry, or the
	// default the engine would use (the slot-th selected channel; "-" when
	// the selection is narrower than the slot index).
	std::wstring slotValue(bool output, int slot) const;
	// True while the side has no explicit list (the values are implicit).
	bool sideDefaulted(bool output) const;
	bool slotSilent(bool output, int slot) const;
	// An explicit entry that does not resolve into the current selection;
	// the engine would disable the filter and pass audio through.
	bool slotChannelMissing(bool output, int slot) const;

	// Assign a channel (or L"-") to a slot. The first edit on a defaulted
	// side materializes the whole list from the effective values, because a
	// partial list is not expressible in the config grammar.
	void pickSlot(bool output, int slot, const std::wstring& value);
	// Back to the implicit default: the side's list disappears from the line.
	void clearSide(bool output);

	const std::vector<std::wstring>& inputFill() const;
	const std::vector<std::wstring>& outputFill() const;
	const std::vector<std::wstring>& selectedChannels() const;

private:
	VST3BusLayout sideLayout(bool output) const;
	std::vector<std::wstring>& sideFill(bool output);
	const std::vector<std::wstring>& sideFill(bool output) const;
	std::wstring defaultValue(int slot) const;

	std::optional<VST3BusContract> contract;
	std::vector<std::wstring> inputList;
	std::vector<std::wstring> outputList;
	std::vector<std::wstring> selection;
};
