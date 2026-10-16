/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

#include "filters/GraphicEQCommand.h"
#include "GraphicEQFilterGUI.h"
#include "GraphicEQFilterGUIFactory.h"
#include "../FilterGUIFactoryRegistry.h"
#include "../widgets/FrequencyPlotScene.h"

REGISTER_FILTER_GUI_FACTORY(FilterGUIFactoryOrder::GraphicEQ, GraphicEQFilterGUIFactory)

GraphicEQFilterGUIFactory::GraphicEQFilterGUIFactory()
{
}

void GraphicEQFilterGUIFactory::initialize(FilterTable* filterTable)
{
	this->filterTable = filterTable;
}

void GraphicEQFilterGUIFactory::startOfFile(const QString& configPath)
{
	this->configPath = configPath;
}

IFilterGUI* GraphicEQFilterGUIFactory::createFilterGUI(QString& command, QString& parameters)
{
	GraphicEQFilterGUI* result = nullptr;

	if (command == "GraphicEQ")
	{
		// Parse the node list with the same shared parser the engine factory uses,
		// straight into the Qt-free struct, instead of building a throwaway
		// GraphicEQFilter (with its IR/FFT setup) just to read its nodes back.
		GraphicEQCommand cmd;
		cmd.parse(parameters.toStdWString());
		result = new GraphicEQFilterGUI(cmd.nodes, configPath, filterTable);
	}

	return result;
}
