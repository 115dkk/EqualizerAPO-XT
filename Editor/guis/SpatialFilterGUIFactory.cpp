/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SpatialFilterGUIFactory.h"

#include "Editor/FilterGUIFactoryRegistry.h"
#include "Editor/FilterTable.h"
#include "Editor/widgets/cards/HilbertCardEditor.h"
#include "Editor/widgets/cards/VelvetCardEditor.h"
#include "devices/AbstractAPOInfo.h"
#include "filters/HilbertCommand.h"
#include "filters/VelvetCommand.h"
#include "audio/ChannelLayout.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::Spatial,
	SpatialFilterGUIFactory)

void SpatialFilterGUIFactory::initialize(FilterTable* table)
{
	const std::shared_ptr<AbstractAPOInfo> device =
		table == nullptr ? nullptr : table->getSelectedDevice();
	const unsigned value = device == nullptr ? 0 : device->getSampleRate();
	sampleRate = value == 0 ? 48000 : value;
	deviceChannels = device == nullptr ? std::vector<std::wstring>()
		: ChannelLayout::getChannelNames(device->getChannelCount(),
			device->getChannelMask());
}

QList<FilterTemplate> SpatialFilterGUIFactory::createFilterTemplates()
{
	return {
		FilterTemplate(tr("Hilbert transform"),
			"Hilbert: Shift=ALL Direction=-90",
			QStringList(tr("Phase & Time"))),
		FilterTemplate(tr("Velvet decorrelator"),
			"Velvet: Mode=Dynamic Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136",
			QStringList(tr("Advanced filters")))
	};
}

IFilterGUI* SpatialFilterGUIFactory::createFilterGUI(QString& command,
	QString& parameters)
{
	if (command == QStringLiteral("Hilbert"))
	{
		HilbertCommand parsed;
		std::wstring error;
		const bool valid = HilbertCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed, &error);
		return new HilbertCardEditor(parsed, sampleRate, deviceChannels,
			valid ? QString() : QString::fromStdWString(error));
	}
	if (command == QStringLiteral("Velvet"))
	{
		VelvetCommand parsed;
		std::wstring error;
		const bool valid = VelvetCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed, &error);
		return new VelvetCardEditor(parsed,
			valid ? QString() : QString::fromStdWString(error));
	}
	return nullptr;
}
