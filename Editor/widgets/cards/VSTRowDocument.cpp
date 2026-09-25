/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VSTRowDocument.h"

#include <utility>

VSTRowDocument::VSTRowDocument(const std::optional<VST3BusContract>& contract, bool legacyStereoInput,
	std::vector<std::wstring> inputFill, std::vector<std::wstring> outputFill)
	: busModel(contract, legacyStereoInput)
{
	fillModel.setContract(busModel.contract());
	fillModel.setFill(std::move(inputFill), std::move(outputFill));
}

const VSTBusModel& VSTRowDocument::bus() const noexcept
{
	return busModel;
}

const VSTSlotFillModel& VSTRowDocument::fill() const noexcept
{
	return fillModel;
}

void VSTRowDocument::setLayouts(VST3BusLayout input, VST3BusLayout output)
{
	if (input != busModel.input())
		fillModel.clearSide(false);
	if (output != busModel.output())
		fillModel.clearSide(true);
	busModel.setLayouts(input, output);
	fillModel.setContract(busModel.contract());
}

void VSTRowDocument::clearLayouts()
{
	busModel.clear();
	fillModel.setContract(busModel.contract());
	clearFill();
}

void VSTRowDocument::setSelectedChannels(std::vector<std::wstring> names)
{
	fillModel.setSelectedChannels(std::move(names));
}

void VSTRowDocument::pickSlot(bool output, int slot, const std::wstring& value)
{
	fillModel.pickSlot(output, slot, value);
}

void VSTRowDocument::clearFill()
{
	fillModel.clearSide(false);
	fillModel.clearSide(true);
}
