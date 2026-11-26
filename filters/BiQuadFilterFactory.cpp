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

#include <vector>

#include "runtime/memory/AlignedMemory.h"
#include "BiQuadFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "services/logging/Logging.h"
#include "BiQuadFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::BiQuad, BiQuadFilterFactory, L"Filter")

using std::wstring;

BiQuadFilterFactory::BiQuadFilterFactory()
{
}

LineOutcome BiQuadFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	BiQuadCommand cmd;
	wstring error;
	std::vector<wstring> traceNotes;
	const bool parsed = BiQuadCommand::parse(command, parameters, cmd, error, &traceNotes);
	// A card-list rebuild parses each row several times. Logging only at the
	// engine boundary avoids repeated file I/O while preserving engine traces.
	for (const wstring& note : traceNotes)
		TraceFStatic(L"%s", note.c_str());
	if (!parsed)
	{
		if (!error.empty())
			return LineOutcome::malformed(std::move(error));
		if (command.starts_with(L"Filter"))
			return LineOutcome::consumed();
		return LineOutcome::notMine();
	}

	return LineOutcome::produced(singleFilter(makeFilter<BiQuadFilter>(
		cmd.type, cmd.dbGain, cmd.freq, cmd.bandwidthOrQOrS, cmd.isBandwidthOrS, cmd.isCornerFreq)));
}
