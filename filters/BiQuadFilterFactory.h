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
#include "BiQuad.h"
#include "BiQuadCommand.h"

class BiQuadFilterFactory : public IFilterFactory
{
public:
	BiQuadFilterFactory();
	FilterVector createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;

	// Parses a "Filter:" config line into a BiQuadCommand. This is the single
	// owner of the BiQuad config-line grammar: createFilter() uses it to build
	// the engine filter, and the Editor uses it to populate the BiQuad GUI
	// without constructing a throwaway filter. Returns true when a valid BiQuad
	// command was recognized. `parameters` may be altered in place (decimal-mark
	// normalization), exactly as createFilter() did before.
	static bool parseCommand(const std::wstring& command, std::wstring& parameters, BiQuadCommand& out);

private:
	static double getFreq(const std::wstring& freqString);
};
