/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

#include "filters/graphicEq/GainIterator.h"

// Plain, Qt-free description of a parsed "GraphicEQ:" config line. It holds the
// node list (freq / dB-gain pairs) exactly as GraphicEQFilterFactory::createFilter
// extracts it from the parameter string, already sorted by frequency the same
// way the factory sorts before handing the nodes to the GraphicEQFilter
// constructor. A GraphicEQCommand therefore fully determines the engine filter
// without going through a throwaway GraphicEQFilter instance.
//
// The engine (GraphicEQFilterFactory) and the Editor GUI share one parse routine
// that fills this struct.
//
// FilterNode is reused as the node element so the struct carries the identical
// {freq, dbGain} representation the engine already uses; this keeps the struct
// free of any extra conversion when building the filter.
struct GraphicEQCommand
{
	std::vector<FilterNode> nodes;

	// Parses the parameter string of a "GraphicEQ:" line into the node list:
	// when the string contains no '.', commas are treated as decimal marks and
	// replaced by periods; numbers matching [-+0-9.eE]+ are read with wcstod and
	// paired as (freq, gain); a trailing unpaired number is dropped; the nodes
	// are sorted by frequency. The parsed nodes replace any current contents.
	void parse(const std::wstring& parameters);

	// Re-creates the canonical parameter string for this command, i.e. the
	// "<freq> <gain>; <freq> <gain>; ..." node list that GraphicEQFilterGUI::store()
	// emits. Each value is formatted with the C "%g" default (six
	// significant digits, trailing zeros stripped), matching QString::arg(double),
	// and pairs are joined with "; ". This is the single owner of the GraphicEQ
	// serialization format shared by the Editor GUI so the written config line
	// stays consistent with what the parser accepts.
	std::wstring serialize() const;
};
