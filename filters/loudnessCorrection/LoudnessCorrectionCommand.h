/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch

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

// Single owner of the "LoudnessCorrection:" config-line grammar, shared by
// the engine factory and the Editor GUI. It replaces the regex grammar that
// used to live in LoudnessCorrectionFilter::FilterParameters::deSerialize and
// the hand-built string in LoudnessCorrectionFilterGUI::store.
struct LoudnessCorrectionCommand
{
	bool state = true;

	// Integer dB values in the grammar; carried as float because the filter
	// computes with them.
	float referenceLevel = 0.0f;
	float referenceOffset = 0.0f;

	// Optional in the grammar, limited to [0, 1]; defaults to full correction.
	float attenuation = 1.0f;

	// Canonical parameter string: "State <0|1> ReferenceLevel <int>
	// ReferenceOffset <int> Attenuation <value>". Attenuation keeps the GUI's
	// historical formatting: exactly 0 or 1 is written as "0.0"/"1.0", other
	// values with the C "%g" default, which matches QString::arg(double) for
	// the spin box's two-decimal granularity.
	std::wstring serialize() const;

	// Returns true when command names a LoudnessCorrection line and the
	// required State/ReferenceLevel/ReferenceOffset parameters parse; a
	// missing Attenuation falls back to 1.0 like before.
	static bool parse(const std::wstring& command, const std::wstring& parameters, LoudnessCorrectionCommand& out);
};
