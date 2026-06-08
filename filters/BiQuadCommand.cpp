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

#include "BiQuadCommand.h"

#include <unordered_map>

bool biquadTypeFromName(const std::wstring& name, BiQuad::Type& outType)
{
	// Single owner of the config keyword -> BiQuad type vocabulary. These are the
	// exact entries the BiQuadFilterFactory parser relied on before, so the set
	// of accepted keywords is unchanged.
	static const std::unordered_map<std::wstring, BiQuad::Type> nameToType = {
		{L"PK", BiQuad::PEAKING},
		{L"PEQ", BiQuad::PEAKING},
		{L"Modal", BiQuad::PEAKING},
		{L"LP", BiQuad::LOW_PASS},
		{L"HP", BiQuad::HIGH_PASS},
		{L"LPQ", BiQuad::LOW_PASS},
		{L"HPQ", BiQuad::HIGH_PASS},
		{L"BP", BiQuad::BAND_PASS},
		{L"LS", BiQuad::LOW_SHELF},
		{L"HS", BiQuad::HIGH_SHELF},
		{L"LSC", BiQuad::LOW_SHELF},
		{L"HSC", BiQuad::HIGH_SHELF},
		{L"NO", BiQuad::NOTCH},
		{L"AP", BiQuad::ALL_PASS},
	};

	auto it = nameToType.find(name);
	if (it == nameToType.end())
		return false;
	outType = it->second;
	return true;
}

const wchar_t* biquadTypeTitle(BiQuad::Type type)
{
	switch (type)
	{
	case BiQuad::PEAKING:
		return L"Peaking";
	case BiQuad::LOW_PASS:
		return L"Low-pass";
	case BiQuad::HIGH_PASS:
		return L"High-pass";
	case BiQuad::BAND_PASS:
		return L"Band-pass";
	case BiQuad::LOW_SHELF:
		return L"Low-shelf";
	case BiQuad::HIGH_SHELF:
		return L"High-shelf";
	case BiQuad::NOTCH:
		return L"Notch";
	case BiQuad::ALL_PASS:
		return L"All-pass";
	}
	return L"Biquad";
}
