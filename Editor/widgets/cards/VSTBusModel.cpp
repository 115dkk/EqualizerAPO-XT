/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "VSTBusModel.h"

VSTBusModel::VSTBusModel(const std::optional<VST3BusContract>& contract, bool legacyStereoInput)
	: currentContract(contract)
{
	if (!currentContract && legacyStereoInput)
	{
		currentContract = VST3BusContract{VST3BusLayout::Stereo, VST3BusLayout::Auto};
		migratedLegacy = true;
	}
}

VST3BusLayout VSTBusModel::input() const noexcept
{
	return currentContract ? currentContract->input : VST3BusLayout::Auto;
}

VST3BusLayout VSTBusModel::output() const noexcept
{
	return currentContract ? currentContract->output : VST3BusLayout::Auto;
}

const std::optional<VST3BusContract>& VSTBusModel::contract() const noexcept
{
	return currentContract;
}

bool VSTBusModel::migratedLegacyStereoInput() const noexcept
{
	return migratedLegacy;
}

void VSTBusModel::setLayouts(VST3BusLayout input, VST3BusLayout output)
{
	currentContract = VST3BusContract{input, output};
	migratedLegacy = false;
}

void VSTBusModel::clear()
{
	currentContract.reset();
	migratedLegacy = false;
}
