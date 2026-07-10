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

// Plain, Qt-free description of a parsed "Delay:" config line. It holds exactly
// the two user-facing parameters that DelayFilterFactory::createFilter feeds
// into the DelayFilter constructor (the delay amount and whether it is given in
// milliseconds rather than samples), so a DelayCommand fully determines the
// engine filter without going through a throwaway DelayFilter instance.
//
// The engine (DelayFilterFactory) and the Editor GUI share one parse routine
// that fills this struct.
struct DelayCommand
{
	double delay = 0.0;
	// true when the delay is expressed in milliseconds, false for samples. This
	// is the bool passed to the DelayFilter constructor.
	bool isMs = true;

	// Re-creates the canonical "<delay> ms" / "<delay> samples" parameter string
	// for this command. This is the single owner of the Delay serialization
	// format shared by the Editor's DelayFilterGUI::store() so the written
	// config line stays consistent with what the parser accepts.
	std::wstring serialize() const;
};
