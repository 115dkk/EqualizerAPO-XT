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

#include "helpers/MemoryHelper.h"
#include "helpers/LogHelper.h"
#include "GraphicEQFilter.h"
#include "GraphicEQCommand.h"
#include "filters/FilterFactoryRegistry.h"
#include "GraphicEQFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::GraphicEQ, GraphicEQFilterFactory)

using std::vector;
using std::wstring;

vector<IFilter*> GraphicEQFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	GraphicEQFilter* filter = nullptr;

	if (command == L"GraphicEQ")
	{
		// Parse the node list into the shared, Qt-free struct so the engine and the
		// Editor build the filter from the exact same parsed values. The struct
		// reproduces the previous inline parse (comma/period handling, number
		// regex, freq/gain pairing and frequency sort) verbatim, so the resulting
		// node list - and therefore the GraphicEQFilter - is unchanged.
		GraphicEQCommand cmd;
		cmd.parse(parameters);

		TraceF(L"Graphic equalizer with %d nodes", cmd.nodes.size());

		filter = MemoryHelper::construct<GraphicEQFilter>(cmd.nodes, 16384);
	}

	if (filter == nullptr)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
