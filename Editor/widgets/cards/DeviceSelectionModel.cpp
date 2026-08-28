/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "DeviceSelectionModel.h"

#include "filters/DeviceCommand.h"

void DeviceSelectionModel::load(const QString& parameters, const QList<DeviceEntry>& devices)
{
	chipList.clear();
	// The legacy dialog treated the literal lowercase "all" line as the
	// all-devices state and ignored individual matches; mirror that exactly.
	all = parameters.trimmed() == QStringLiteral("all");

	// Match through the shared codec so a chip is pre-selected exactly when the
	// engine would match that device for this pattern.
	DeviceCommand cmd = DeviceCommand::fromPattern(parameters.toStdWString());

	for (const DeviceEntry& entry : devices)
	{
		DeviceChipInfo chip;
		chip.name = entry.name;
		chip.connection = entry.connection;
		chip.deviceString = entry.deviceString;
		chip.installed = entry.installed;
		chip.isInput = entry.isInput;
		chip.selected = !all && cmd.matches(entry.deviceString.toStdWString());
		chipList.append(chip);
	}
}

const QList<DeviceChipInfo>& DeviceSelectionModel::chips() const
{
	return chipList;
}

bool DeviceSelectionModel::allSelected() const
{
	return all;
}

void DeviceSelectionModel::setAllSelected(bool on)
{
	all = on;
}

void DeviceSelectionModel::toggle(const QString& deviceString)
{
	const bool narrowing = all;
	if (narrowing)
	{
		all = false;
		for (DeviceChipInfo& chip : chipList)
			chip.selected = false;
	}
	for (DeviceChipInfo& chip : chipList)
	{
		if (chip.deviceString == deviceString)
		{
			chip.selected = narrowing || !chip.selected;
			return;
		}
	}
}

QString DeviceSelectionModel::serialize() const
{
	// Byte-identical with the legacy DeviceFilterGUIDialog::getPattern(): "all"
	// when the all-devices state is set, otherwise each selected device's device
	// string joined with "; " in list order (output devices then input).
	if (all)
		return QStringLiteral("all");

	QString pattern;
	for (const DeviceChipInfo& chip : chipList)
	{
		if (!chip.selected)
			continue;
		if (!pattern.isEmpty())
			pattern += QStringLiteral("; ");
		pattern += chip.deviceString;
	}
	return pattern;
}
