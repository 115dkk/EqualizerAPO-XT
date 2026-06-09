/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

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

#include "helpers/VSTPluginLibrary.h"
#include "helpers/StringHelper.h"
#include "filters/VSTPluginCommand.h"
#include "VSTPluginFilterGUI.h"
#include "VSTPluginFilterGUIFactory.h"

using std::list;
using std::unordered_map;
using std::wstring;

QList<FilterTemplate> VSTPluginFilterGUIFactory::createFilterTemplates()
{
	QList<FilterTemplate> list;
	list.append(FilterTemplate(tr("VST plugin"), "VSTPlugin:", QStringList(tr("Plugins"))));
	return list;
}

IFilterGUI* VSTPluginFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	VSTPluginFilterGUI* result = nullptr;

	if (command == "VSTPlugin")
	{
		// Parse straight into the shared command struct. This reuses the engine's
		// exact parameter grammar without building (and immediately destroying) a
		// real VSTPluginFilter, and never loads a plugin binary: getInstance only
		// returns the cached library object, the DLL is loaded later by the GUI.
		VSTPluginCommand cmd = VSTPluginCommand::parse(L"", parameters.toStdWString());
		std::shared_ptr<VSTPluginLibrary> library = cmd.libraryPath.empty() ? nullptr : VSTPluginLibrary::getInstance(cmd.libraryPath);
		if (library != nullptr)
			result = new VSTPluginFilterGUI(library, cmd.chunkData, cmd.paramMap);
		else
			result = new VSTPluginFilterGUI(VSTPluginLibrary::getInstance(L""), L"", unordered_map<wstring, float>());
	}

	return result;
}
