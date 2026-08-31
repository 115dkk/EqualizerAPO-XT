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
// that fills this struct.
struct BiQuadCommand
{
	BiQuad::Type type = BiQuad::PEAKING;
	double dbGain = 0.0;
	double freq = 0.0;
	double bandwidthOrQOrS = 0.0;
	bool isBandwidthOrS = false;
	bool isCornerFreq = false;
	// Whether the line said "Order" out loud. An all-pass without it is a
	// 2nd-order section, which is the only reading that keeps every
	// configuration written before the order existed sounding the same - but a
	// default hidden in the grammar cannot be changed later and cannot be read
	// off the file, so the Editor uses this to fill it in.
	bool orderWasExplicit = false;
	// The grammar currently only accepts lines beginning with "ON"; OFF lines do
	// not parse. The flag is carried so callers do not have to re-derive it.
	bool enabled = true;
};

// There is deliberately no BiQuadCommand::serialize() counterpart to the
// PreampCommand / VSTPluginCommand serializers. Those
// round-trip because their parse is symmetric; the BiQuad parse is not. A "Filter:"
// line cannot be reproduced from this struct alone, because BiQuadFilterFactory's
// parser is intentionally lossy and normalizing:
//   - A missing Q / BW / slope token is replaced by a synthesized default
//     (1 / sqrt(2) for LP/HP/BP, 0.9 for shelves, 30 for notch), so "no token" and
//     "a token whose value equals the default" collapse to identical fields.
//   - LP/LPQ, HP/HPQ and LS/LSC all map to one BiQuad::Type; the spelling that
//     was written is not recorded.
//   - A shelf slope is stored divided by 12, not as it was typed.
// The Editor's BiQuadFilterGUI::store() is therefore the single serializer, and it
// emits from the GUI mode selectors (the Fixed/Q/BW/Slope and centre/corner combo
// boxes) - state this Qt-free struct does not carry. A shared serializer would have
// to live in the GUI and read those widgets, so it would relocate store() rather
// than remove a duplicate: the engine never serializes BiQuad lines, so no second
// serializer exists to consolidate.

// Maps a config-line type keyword (e.g. L"PK", L"LSC", L"Modal") to a BiQuad
// type. Returns false for unknown keywords. This is the single owner of the
// keyword -> type vocabulary used by both the parser and the Editor.
bool biquadTypeFromName(const std::wstring& name, BiQuad::Type& outType);

// Human-readable title for a BiQuad type, e.g. L"Peaking", L"Low-shelf". This is
// the single owner of the type -> title mapping shared by the engine-side parse
// log and the Editor's filter card model. The returned pointer is
// to a static literal and stays valid for the lifetime of the process.
const wchar_t* biquadTypeTitle(BiQuad::Type type);
