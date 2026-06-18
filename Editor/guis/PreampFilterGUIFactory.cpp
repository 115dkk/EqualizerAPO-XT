/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "PreampFilterGUI.h"
#include <filters/PreampFilterFactory.h>
#include "PreampFilterGUIFactory.h"
#include "../FilterGUIFactoryRegistry.h"

REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::Preamp, PreampFilterGUIFactory)

QList<FilterTemplate> PreampFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("Preamp (Preamplification)"), "Preamp: 0 dB", QStringList(tr("Basic filters"))));
	return list;
}

IFilterGUI* PreampFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	PreampFilterGUI* result = nullptr;

	if (command == "Preamp")
	{
		// Parse the config line through the engine's single owning parse routine
		// and populate the GUI directly from the resulting command, instead of
		// constructing a throwaway PreampFilter just to read its dB gain back.
		std::wstring commandWStr = command.toStdWString();
		std::wstring paramWStr = parameters.toStdWString();
		PreampCommand cmd;
		// A 0 dB line parses as valid (just noOp), so the Editor shows the preamp
		// GUI for it like the canonical card editor does; only a malformed
		// parameter (cmd.valid == false) yields no GUI. The former throwaway-filter
		// hack returned null for 0 dB because the engine skips the no-op filter,
		// which left the legacy path without a preamp editor for that line.
		if (PreampFilterFactory::parseCommand(commandWStr, paramWStr, cmd) && cmd.valid)
			result = new PreampFilterGUI(cmd.dbGain);
	}

	return result;
}
