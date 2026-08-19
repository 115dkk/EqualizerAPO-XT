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

#include "engine/IFilterFactory.h"
#include "engine/IFilter.h"
#include "DelayCommand.h"

class DelayFilterFactory : public ParseReportingFactory
{
public:
	FilterVector createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;

	// Parses a "Delay:" config line into a DelayCommand. The line grammar itself
	// lives in DelayCommand::parse (which the Editor GUIs use directly, because
	// they must open an editor even for a no-op line); this wrapper adds the
	// engine's decisions on top. Returns true only when the line names a delay
	// that should produce a DelayFilter (delay > 0 with a "ms" or "samples"
	// unit). A 0-length delay is a no-op and an unknown/missing unit is
	// rejected, so both return false, exactly as createFilter() decided before.
	// `malformed` (optional) is set when the command keyword was "Delay" but
	// the parameter text did not parse - the case createFilter() reports as a
	// parse error, as opposed to the deliberate no-op of a zero delay.
	static bool parseCommand(const std::wstring& command, const std::wstring& parameters, DelayCommand& out,
		bool* malformed = nullptr);
};
