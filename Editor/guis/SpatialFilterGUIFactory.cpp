/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SpatialFilterGUIFactory.h"

#include "Editor/FilterGUIFactoryRegistry.h"
#include "Editor/widgets/cards/FilterCardEditorFactory.h"

// cppcheck's standalone parser does not expand the static-registration macro.
// cppcheck-suppress unknownMacro
REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::Spatial,
	SpatialFilterGUIFactory)

void SpatialFilterGUIFactory::initialize(FilterTable* table)
{
	filterTable = table;
}

IFilterGUI* SpatialFilterGUIFactory::createFilterGUI(QString& command,
	QString& parameters)
{
	// The frozen LegacyRows path shows the Hilbert and Velvet card editors
	// too; the cards' own registered creators build them, so both modes read
	// the device's sample rate and channels when the row is built.
	if (command == QStringLiteral("Hilbert") || command == QStringLiteral("Velvet"))
		return FilterCardEditorFactory::create(filterTable, command, parameters);
	return nullptr;
}
