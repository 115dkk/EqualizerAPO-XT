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

#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "DelayFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "DelayFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Delay, DelayFilterFactory, L"Delay")

using std::vector;
using std::wstring;

bool DelayFilterFactory::parseCommand(const wstring& command, const wstring& parameters, DelayCommand& out)
{
	if (command != L"Delay")
		return false;

	// The codec owns the grammar (and accepts zero, so the Editor can still
	// open a card for a no-op line); the engine-only decisions below are the
	// trace and the no-op gate.
	if (!DelayCommand::parse(parameters, out))
		return false;

	// A 0-length delay is a no-op: it produces no filter so the chain avoids one
	// virtual call and one ring-buffer update per block. Report it in the trace
	// and reject the command so no DelayFilter is built.
	if (out.delay == 0.0)
	{
		TraceFStatic(L"Skipping no-op delay (0 %s)", out.isMs ? L"ms" : L"samples");
		return false;
	}

	TraceFStatic(L"Delaying by %g %s", out.delay, out.isMs ? L"ms" : L"samples");
	return true;
}

FilterVector DelayFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	DelayCommand cmd;
	if (!parseCommand(command, parameters, cmd))
		return {};

	return singleFilter(makeFilter<DelayFilter>(cmd.delay, cmd.isMs));
}
