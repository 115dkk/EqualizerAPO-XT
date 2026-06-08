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

#include "BiQuad.h"

// Plain, Qt-free description of a parsed "Filter:" BiQuad config line. It holds
// exactly the user-facing parameters that BiQuadFilterFactory::createFilter
// feeds into the BiQuadFilter constructor, so a BiQuadCommand fully determines
// the engine filter without going through a throwaway BiQuadFilter instance.
//
// The engine (BiQuadFilterFactory) and the Editor GUI share one parse routine
// that fills this struct, eliminating the former "build a real filter just to
// read its fields back" hack in the Editor.
struct BiQuadCommand
{
	BiQuad::Type type = BiQuad::PEAKING;
	double dbGain = 0.0;
	double freq = 0.0;
	double bandwidthOrQOrS = 0.0;
	bool isBandwidthOrS = false;
	bool isCornerFreq = false;
	// The grammar currently only accepts lines beginning with "ON"; OFF lines do
	// not parse. The flag is carried so callers do not have to re-derive it.
	bool enabled = true;
};

// Maps a config-line type keyword (e.g. L"PK", L"LSC", L"Modal") to a BiQuad
// type. Returns false for unknown keywords. This is the single owner of the
// keyword -> type vocabulary used by both the parser and the Editor.
bool biquadTypeFromName(const std::wstring& name, BiQuad::Type& outType);

// Human-readable title for a BiQuad type, e.g. L"Peaking", L"Low-shelf". This is
// the single owner of the type -> title mapping shared by the engine-side parse
// log and the Editor's filter card model (F042 dedup). The returned pointer is
// to a static literal and stays valid for the lifetime of the process.
const wchar_t* biquadTypeTitle(BiQuad::Type type);
