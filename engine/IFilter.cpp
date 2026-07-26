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
#include "IFilter.h"

#include "ConfigLoadTrace.h"
#include "FilterEngine.h"
#include "IFilterFactory.h"

// Defined here rather than in IFilterFactory.h because the reporting path needs
// FilterEngine, and every filter's header includes IFilterFactory.h - pulling the
// engine into all of them would be a real compile-time cost for two functions.
void ParseReportingFactory::initialize(FilterEngine* engine)
{
	reportingEngine = engine;
}

FilterVector ParseReportingFactory::reportParseError(const std::wstring& command, const std::wstring& reason) const
{
	if (reportingEngine != nullptr)
		reportingEngine->reportParseError(command, reason);
	return {};
}
