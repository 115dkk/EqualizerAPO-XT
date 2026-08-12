/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "VSTBusLayoutEditorModel.h"

VSTBusLayoutEditorModel::VSTBusLayoutEditorModel(
	const std::optional<VST3BusContract>& contract, bool legacyStereoInput)
	: currentContract(contract)
{
	// The explicit Input/Output pair is authoritative if a hand-edited line
	// contains both generations of settings. Otherwise the old upmixer flag
	// becomes the equivalent asymmetric contract and is never emitted again.
	if (!currentContract && legacyStereoInput)
	{
		currentContract = VST3BusContract{ VST3BusLayout::Stereo, VST3BusLayout::Auto };
		migratedLegacy = true;
	}
}

VST3BusLayout VSTBusLayoutEditorModel::input() const noexcept
{
	return currentContract ? currentContract->input : VST3BusLayout::Auto;
}

VST3BusLayout VSTBusLayoutEditorModel::output() const noexcept
{
	return currentContract ? currentContract->output : VST3BusLayout::Auto;
}

const std::optional<VST3BusContract>& VSTBusLayoutEditorModel::contract() const noexcept
{
	return currentContract;
}

bool VSTBusLayoutEditorModel::migratedLegacyStereoInput() const noexcept
{
	return migratedLegacy;
}

void VSTBusLayoutEditorModel::setLayouts(VST3BusLayout input, VST3BusLayout output)
{
	currentContract = VST3BusContract{ input, output };
	migratedLegacy = false;
}

void VSTBusLayoutEditorModel::removeLayouts()
{
	currentContract.reset();
	migratedLegacy = false;
}
