/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SubwooferRoutingFilterGUIFactory.h"

#include "Editor/FilterGUIFactoryRegistry.h"
#include "Editor/FilterTable.h"
#include "Editor/widgets/cards/SubwooferRoutingCardEditor.h"
#include "filters/subwooferRouting/SubwooferRoutingCommand.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::SubwooferRouting,
	SubwooferRoutingFilterGUIFactory)

void SubwooferRoutingFilterGUIFactory::initialize(FilterTable* table)
{
	filterTable = table;
	configPath = table == nullptr ? QString() : table->getConfigPath();
}

void SubwooferRoutingFilterGUIFactory::startOfFile(
	const QString& path)
{
	configPath = path;
}

IFilterGUI* SubwooferRoutingFilterGUIFactory::createFilterGUI(
	QString& command, QString& parameters)
{
	if (command != QStringLiteral("SubwooferRouting"))
		return nullptr;

	SubwooferRoutingCommand parsed;
	QString parseError;
	if (parameters.trimmed().isEmpty())
	{
		parsed.form = SubwooferRoutingCommand::Form::State;
	}
	else
	{
		std::wstring error;
		if (!SubwooferRoutingCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed, &error))
		{
			parseError = QString::fromStdWString(error);
		}
	}

	// Read when the row is built, not at initialize: the device can change
	// after the table is set up. This frozen path keeps its 48 kHz stand-in
	// for an unknown rate; the card path passes 0 (no device checks).
	const unsigned rate = filterTable == nullptr ? 0 : filterTable->deviceSampleRate();
	return new SubwooferRoutingCardEditor(filterTable, parsed, configPath,
		rate == 0 ? 48000 : rate, parameters, parseError);
}
