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
#include "PreampCommand.h"

class PreampFilterFactory : public IFilterFactory
{
public:
	FilterVector createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;

	// Parses a "Preamp:" config line into a PreampCommand. This is the single
	// owner of the preamp grammar: createFilter() uses it to decide whether to
	// build a PreampFilter (and with which gain), and the Editor uses it to
	// populate the preamp GUI without constructing a throwaway PreampFilter.
	// Returns true when the command keyword was "Preamp"; the out struct's
	// valid/noOp flags then describe the parse outcome. The number parsing and
	// defaulting are kept byte-identical to the previous inline code.
	static bool parseCommand(const std::wstring& command, const std::wstring& parameters, PreampCommand& out);
};
