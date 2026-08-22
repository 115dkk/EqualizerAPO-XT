/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "VSTSlotFillModel.h"

#include "audio/ChannelLayout.h"

void VSTSlotFillModel::setContract(const std::optional<VST3BusContract>& newContract)
{
	contract = newContract;
}

void VSTSlotFillModel::setFill(std::vector<std::wstring> input, std::vector<std::wstring> output)
{
	inputList = std::move(input);
	outputList = std::move(output);
}

void VSTSlotFillModel::setSelectedChannels(std::vector<std::wstring> names)
{
	selection = std::move(names);
}

bool VSTSlotFillModel::railPresent(bool output) const
{
	return sideLayout(output) != VST3BusLayout::Auto;
}

bool VSTSlotFillModel::latchPresent() const
{
	return railPresent(false) && railPresent(true);
}

int VSTSlotFillModel::slotCount(bool output) const
{
	return vst3BusLayoutChannelCount(sideLayout(output));
}

std::wstring VSTSlotFillModel::slotRole(bool output, int slot) const
{
	const std::vector<std::wstring> roles = vst3BusLayoutChannelNames(sideLayout(output));
	if (slot < 0 || slot >= static_cast<int>(roles.size()))
		return std::wstring();
	return roles[static_cast<size_t>(slot)];
}

std::wstring VSTSlotFillModel::slotValue(bool output, int slot) const
{
	const std::vector<std::wstring>& fill = sideFill(output);
	if (!fill.empty())
	{
		if (slot < 0 || slot >= static_cast<int>(fill.size()))
			return std::wstring();
		return fill[static_cast<size_t>(slot)];
	}
	return defaultValue(slot);
}

bool VSTSlotFillModel::sideDefaulted(bool output) const
{
	return sideFill(output).empty();
}

bool VSTSlotFillModel::slotSilent(bool output, int slot) const
{
	return slotValue(output, slot) == L"-";
}

bool VSTSlotFillModel::slotChannelMissing(bool output, int slot) const
{
	if (sideDefaulted(output))
		return false;
	// No selection context (no device, previews): indeterminate, not wrong.
	if (selection.empty())
		return false;
	const std::wstring value = slotValue(output, slot);
	if (value.empty() || value == L"-")
		return false;
	// The same resolver the engine uses, against the same selection, so the
	// warning matches what would actually pass audio through.
	return ChannelLayout::getChannelIndex(value, selection) < 0;
}

void VSTSlotFillModel::pickSlot(bool output, int slot, const std::wstring& value)
{
	const int count = slotCount(output);
	if (slot < 0 || slot >= count)
		return;
	std::vector<std::wstring>& fill = sideFill(output);
	if (fill.empty())
	{
		fill.reserve(static_cast<size_t>(count));
		for (int i = 0; i < count; i++)
			fill.push_back(defaultValue(i));
	}
	fill[static_cast<size_t>(slot)] = value;
}

void VSTSlotFillModel::clearSide(bool output)
{
	sideFill(output).clear();
}

const std::vector<std::wstring>& VSTSlotFillModel::inputFill() const
{
	return inputList;
}

const std::vector<std::wstring>& VSTSlotFillModel::outputFill() const
{
	return outputList;
}

const std::vector<std::wstring>& VSTSlotFillModel::selectedChannels() const
{
	return selection;
}

VST3BusLayout VSTSlotFillModel::sideLayout(bool output) const
{
	if (!contract)
		return VST3BusLayout::Auto;
	return output ? contract->output : contract->input;
}

std::vector<std::wstring>& VSTSlotFillModel::sideFill(bool output)
{
	return output ? outputList : inputList;
}

const std::vector<std::wstring>& VSTSlotFillModel::sideFill(bool output) const
{
	return output ? outputList : inputList;
}

std::wstring VSTSlotFillModel::defaultValue(int slot) const
{
	if (slot < 0 || slot >= static_cast<int>(selection.size()))
		return L"-";
	return selection[static_cast<size_t>(slot)];
}
