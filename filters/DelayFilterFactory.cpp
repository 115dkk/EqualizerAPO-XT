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
#include "DelayCommand.h"
#include "DelayFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "DelayFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Delay, DelayFilterFactory, L"Delay")

using std::vector;
using std::wstring;

LineOutcome DelayFilterFactory::createFilter(const wstring& configPath,
	wstring& command, wstring& parameters)
{
	if (command != L"Delay")
		return LineOutcome::notMine();

	DelayCommand cmd;
	if (!DelayCommand::parse(parameters, cmd))
	{
		return LineOutcome::malformed(
			L"expected a delay like \"3.5 ms\" or \"160 samples\"");
	}
	if (cmd.delay == 0.0)
	{
		TraceF(L"Skipping no-op delay (0 %s)", cmd.isMs ? L"ms" : L"samples");
		return LineOutcome::consumed();
	}

	TraceF(L"Delaying by %g %s", cmd.delay, cmd.isMs ? L"ms" : L"samples");
	return LineOutcome::produced(singleFilter(
		makeFilter<DelayFilter>(cmd.delay, cmd.isMs)));
}
