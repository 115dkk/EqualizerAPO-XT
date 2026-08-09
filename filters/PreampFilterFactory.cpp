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

bool PreampFilterFactory::parseCommand(const wstring& command, const wstring& parameters, PreampCommand& out)
{
	out = PreampCommand();

	if (command != L"Preamp")
		return false;

	// Conversion to period as decimal mark, if needed
	wstring value = numeric_text::normalizeDecimalComma(parameters);

	double preamp_dB;
	int matched = swscanf_s(value.c_str(), L" %lf dB", &preamp_dB);
	if (matched == 1 && std::isfinite(preamp_dB))
	{
		out.valid = true;
		out.dbGain = preamp_dB;
		// A 0 dB preamp is a no-op: createFilter skips the allocation so the
		// filter chain avoids one virtual call and one pointer setup per block.
		out.noOp = std::abs(preamp_dB) < 1e-9;
	}
	else
	{
		// Malformed parameter: leave out.valid == false. The warning is
		// emitted by createFilter (the engine owns the log line); the
		// Editor path simply discards the GUI for this line.
	}

	return true;
}

FilterVector PreampFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	FilterPtr filter;

	PreampCommand cmd;
	if (parseCommand(command, parameters, cmd))
	{
		if (cmd.valid)
		{
			if (cmd.noOp)
			{
				TraceF(L"Skipping no-op preamp (%g dB)", cmd.dbGain);
			}
			else
			{
				TraceF(L"Adjusting preamp by %g dB", cmd.dbGain);

				filter = makeFilter<PreampFilter>(cmd.dbGain);
			}
		}
		else
		{
			LogF(L"Could not parse preamp value \"%s\"; no preamp was applied", text::trim(parameters).c_str());
		}
	}

	if (filter == nullptr)
		return {};
	return singleFilter(std::move(filter));
}
