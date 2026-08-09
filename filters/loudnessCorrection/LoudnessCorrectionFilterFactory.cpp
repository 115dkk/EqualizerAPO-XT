/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch

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
#define _USE_MATH_DEFINES
#include <cmath>
#include <regex>
#include <sstream>

#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "LoudnessCorrectionCommand.h"
#include "LoudnessCorrectionFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "LoudnessCorrectionFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::LoudnessCorrection, LoudnessCorrectionFilterFactory, L"LoudnessCorrection")

using std::regex;
using std::vector;
using std::wstring;

LoudnessCorrectionFilterFactory::LoudnessCorrectionFilterFactory()
{
}

FilterVector LoudnessCorrectionFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	FilterVector allFilters;

	if (command != L"LoudnessCorrection")
		return allFilters;

	LoudnessCorrectionCommand cmd;
	if (!LoudnessCorrectionCommand::parse(command, parameters, cmd))
	{
		// The parser wants all three of State, ReferenceLevel and
		// ReferenceOffset; a line missing any of them produced nothing at all and
		// said nothing about which.
		return reportParseError(command,
			L"expected State, ReferenceLevel and ReferenceOffset, as in "
			L"\"State 1 ReferenceLevel 75 ReferenceOffset 0\"");
	}

	{
		TraceF(L"Adding loudness correction filter");
		LoudnessCorrectionFilter::FilterParameters filterParameters;
		filterParameters.state = cmd.state;
		filterParameters.referenceLevel = cmd.referenceLevel;
		filterParameters.referenceOffset = cmd.referenceOffset;
		filterParameters.attenuation = cmd.attenuation;
		allFilters.push_back(makeFilter<LoudnessCorrectionFilter>(filterParameters));
	}

	return allFilters;
}
