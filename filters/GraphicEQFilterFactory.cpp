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
#include <regex>

#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "GraphicEQFilter.h"
#include "FilterFactoryRegistry.h"
#include "GraphicEQFilterFactory.h"

REGISTER_FILTER_FACTORY(12, GraphicEQFilterFactory)

using std::find;
using std::regex;
using std::sort;
using std::vector;
using std::wregex;
using std::wsmatch;
using std::wsregex_iterator;
using std::wstring;

static wregex regexNumber(L"[-+0-9.eE]+");

vector<IFilter*> GraphicEQFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	GraphicEQFilter* filter = nullptr;

	if (command == L"GraphicEQ")
	{
		wstring value = parameters;
		if (value.find(L'.') == wstring::npos)
			value = StringHelper::replaceCharacters(value, L",", L".");

		wsregex_iterator begin(value.begin(), value.end(), regexNumber);
		wsregex_iterator end;

		vector<FilterNode> nodes;
		for (wsregex_iterator it = begin; it != end; it++)
		{
			wsmatch freqMatch = *it++;
			if (it != end)
			{
				wsmatch gainMatch = *it;
				double freq = wcstod(freqMatch.str(0).c_str(), nullptr);
				double gain = wcstod(gainMatch.str(0).c_str(), nullptr);
				FilterNode node(freq, gain);
				nodes.push_back(node);
			}
		}
		sort(nodes.begin(), nodes.end());

		TraceF(L"Graphic equalizer with %d nodes", nodes.size());

		void* mem = MemoryHelper::alloc(sizeof(GraphicEQFilter));
		filter = new(mem) GraphicEQFilter(nodes, 16384);
	}

	if (filter == nullptr)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
