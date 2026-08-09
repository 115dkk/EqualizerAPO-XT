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

#include "runtime/memory/MemoryHelper.h"
#include "text/StringHelper.h"
#include "services/logging/LogHelper.h"
#include "DelayFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "DelayFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Delay, DelayFilterFactory, L"Delay")

using std::vector;
using std::wstringstream;
using std::wstring;

bool DelayFilterFactory::parseCommand(const wstring& command, wstring& parameters, DelayCommand& out)
{
	if (command != L"Delay")
		return false;

	// Conversion to period as decimal mark, if needed
	wstring value = StringHelper::normalizeDecimalComma(parameters);

	double delay = -1;
	wstring unit;
	wstringstream stream(value);
	stream >> delay >> unit;

	if (delay < 0)
		return false;

	// A 0-length delay is a no-op: it produces no filter so the chain avoids one
	// virtual call and one ring-buffer update per block. Report it in the trace
	// and reject the command so no DelayFilter is built.
	if (delay == 0.0)
	{
		TraceFStatic(L"Skipping no-op delay (0 %s)", StringHelper::toLowerCase(unit).c_str());
		return false;
	}

	if (StringHelper::toLowerCase(unit) == L"ms")
	{
		TraceFStatic(L"Delaying by %g ms", delay);
		out.delay = delay;
		out.isMs = true;
		return true;
	}

	if (StringHelper::toLowerCase(unit) == L"samples")
	{
		TraceFStatic(L"Delaying by %g samples", delay);
		out.delay = delay;
		out.isMs = false;
		return true;
	}

	return false;
}

FilterVector DelayFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	DelayCommand cmd;
	if (!parseCommand(command, parameters, cmd))
		return {};

	return singleFilter(makeFilter<DelayFilter>(cmd.delay, cmd.isMs));
}
