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

#include "stdafx.h"
#include <sstream>

#include "helpers/MemoryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "DelayFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "DelayFilterFactory.h"

REGISTER_FILTER_FACTORY(9, DelayFilterFactory)

using std::vector;
using std::wstringstream;
using std::wstring;

vector<IFilter*> DelayFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	DelayFilter* filter = nullptr;

	if (command == L"Delay")
	{
		// Conversion to period as decimal mark, if needed
		wstring value = StringHelper::replaceCharacters(parameters, L",", L".");

		double delay = -1;
		wstring unit;
		wstringstream stream(value);
		stream >> delay >> unit;

		if (delay >= 0)
		{
			// A 0-length delay is a no-op: skip allocating the filter so the chain
			// avoids one virtual call and one ring-buffer update per block.
			if (delay == 0.0)
			{
				TraceF(L"Skipping no-op delay (0 %s)", StringHelper::toLowerCase(unit).c_str());
			}
			else if (StringHelper::toLowerCase(unit) == L"ms")
			{
				TraceF(L"Delaying by %g ms", delay);
				void* mem = MemoryHelper::alloc(sizeof(DelayFilter));
				filter = new(mem) DelayFilter(delay, true);
			}
			else if (StringHelper::toLowerCase(unit) == L"samples")
			{
				TraceF(L"Delaying by %g samples", delay);
				void* mem = MemoryHelper::alloc(sizeof(DelayFilter));
				filter = new(mem) DelayFilter(delay, false);
			}
		}
	}

	if (filter == nullptr)
		return vector<IFilter*>(0);
	return vector<IFilter*>(1, filter);
}
