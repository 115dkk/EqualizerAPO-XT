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
#include "IIRCommand.h"

class IIRFilterFactory : public IFilterFactory
{
public:
	IIRFilterFactory();
	FilterVector createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;

	// Parses a "Filter:" IIR config line into an IIRCommand. This is the single
	// owner of the IIR grammar; grammar errors (order below 1, wrong coefficient
	// count) are reported through the log helpers, lines of other filter types
	// just return false.
	static bool parseCommand(const std::wstring& command, const std::wstring& parameters, IIRCommand& out);
};
