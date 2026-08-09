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

#include "stdafx.h"

#include "runtime/memory/MemoryHelper.h"
#include "services/logging/LogHelper.h"
#include "GraphicEQFilter.h"
#include "GraphicEQCommand.h"
#include "filters/FilterFactoryRegistry.h"
#include "GraphicEQFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::GraphicEQ, GraphicEQFilterFactory, L"GraphicEQ")

using std::vector;
using std::wstring;

FilterVector GraphicEQFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	if (command == L"GraphicEQ")
	{
		// Parse the node list into the shared, Qt-free struct so the engine and the
		// Editor build the filter from the exact same parsed values.
		GraphicEQCommand cmd;
		cmd.parse(parameters);

		if (cmd.nodes.empty())
			return reportParseError(command, L"expected frequency/gain pairs, as in \"25 -6; 50 -3; 100 0\"");

		TraceF(L"Graphic equalizer with %d nodes", cmd.nodes.size());

		return singleFilter(makeFilter<GraphicEQFilter>(cmd.nodes, 16384));
	}

	return {};
}
