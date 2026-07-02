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

// Plain description of a parsed "Filter:" IIR config line ("ON IIR Order N
// Coefficients b0 .. bN a0 .. aN"). It holds exactly what
// IIRFilterFactory::createFilter feeds into the IIRFilter constructor.
//
// Like the BiQuad codec, the parse routine lives on the factory
// (IIRFilterFactory::parseCommand) because it reports grammar errors through
// the log helpers; this struct owns the data shape and the serialization.
struct IIRCommand
{
	unsigned order = 0;

	// (order + 1) * 2 values: b0..bN followed by a0..aN, in config-line order.
	std::vector<double> coefficients;

	// Canonical parameter string: "ON IIR Order <order> Coefficients <...>".
	// Each coefficient is formatted with the C "%g" default (six significant
	// digits, trailing zeros stripped), like the GraphicEQ codec.
	std::wstring serialize() const;
};
