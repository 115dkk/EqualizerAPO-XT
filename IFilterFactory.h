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

#pragma once

#include <string>
#include <vector>

#include "helpers/MemoryHelper.h"
#include "IFilter.h"

class FilterEngine;

#pragma AVRT_VTABLES_BEGIN
class IFilterFactory
{
public:
	virtual ~IFilterFactory() {}

	virtual void initialize(FilterEngine* engine) {}
	virtual std::vector<IFilter*> startOfConfiguration() {return std::vector<IFilter*>();}
	virtual std::vector<IFilter*> startOfFile(const std::wstring& configPath) {return std::vector<IFilter*>();}
	// Contract (see the dispatch loop in engine/FilterEngine.Configuration.cpp):
	// the engine offers each config line to every factory in priority order.
	// - Returning one or more filters consumes the line; iteration stops.
	// - Clearing `command` to L"" also consumes the line without producing a
	//   filter. Only control-flow factories (Device/If/Else/EndIf/Eval/Include/
	//   Stage) use this signal; processing factories leave `command` untouched.
	// - Leaving `command` set and returning no filters passes the line on to
	//   the next factory.
	// `parameters` may be normalized in place (e.g. decimal-comma fixes); the
	// engine does not read it back after the call.
	virtual std::vector<IFilter*> createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) = 0;
	virtual std::vector<IFilter*> endOfFile(const std::wstring& configPath) {return std::vector<IFilter*>();}
	virtual std::vector<IFilter*> endOfConfiguration() {return std::vector<IFilter*>();}
};
#pragma AVRT_VTABLES_END
