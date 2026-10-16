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
#include "text/WideString.h"
#include "parser/NumericText.h"
#include <cmath>
#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "PreampFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "PreampFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::Preamp, PreampFilterFactory, L"Preamp")

using std::vector;
using std::wstring;

LineOutcome PreampFilterFactory::createFilter(const wstring& configPath,
	wstring& command, wstring& parameters)
{
	PreampCommand cmd;
	if (!PreampCommand::parse(command, parameters, cmd))
		return LineOutcome::notMine();
	if (!cmd.valid)
	{
		return LineOutcome::malformed(
			L"could not parse the preamp value \"" + text::trim(parameters)
			+ L"\"; no preamp was applied");
	}
	if (cmd.noOp)
	{
		TraceF(L"Skipping no-op preamp (%g dB)", cmd.dbGain);
		return LineOutcome::consumed();
	}

	TraceF(L"Adjusting preamp by %g dB", cmd.dbGain);
	return LineOutcome::produced(singleFilter(makeFilter<PreampFilter>(cmd.dbGain)));
}
