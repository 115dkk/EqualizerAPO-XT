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

// Plain, Qt-free description of a parsed "Preamp:" config line. It holds the
// user-facing dB value plus the two distinctions PreampFilterFactory::createFilter
// makes when turning the line into an engine filter:
//   - valid  : the parameter parsed as "<number> dB". A malformed parameter
//              leaves valid == false (the F019 warning path).
//   - noOp   : the parameter was valid but the gain rounds to 0 dB, which the
//              factory deliberately skips so the chain has no PreampFilter for it.
// With both flags a PreampCommand fully determines whether createFilter emits a
// PreampFilter and with which gain, so the Editor no longer has to build a
// throwaway PreampFilter just to read getDbGain() back.
struct PreampCommand
{
	double dbGain = 0.0;
	bool valid = false;
	bool noOp = false;

	// Serializes this PreampCommand back into the canonical "<dB> dB" parameter
	// string, the same form PreampFilterGUI::store() emits and that the factory
	// parser accepts. Qt-free so both the Editor GUI and the round-trip tests can
	// share it. The dB value is formatted with %g (six significant digits, trailing
	// zeros stripped), matching what QString::arg(double) produced for these values.
	std::wstring serialize() const;
};
