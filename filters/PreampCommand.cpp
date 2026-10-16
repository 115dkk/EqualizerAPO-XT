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

#include "stdafx.h"

#include "PreampCommand.h"

#include <cmath>
#include <cstdio>

#include "parser/NumericText.h"

bool PreampCommand::parse(const std::wstring& command,
	const std::wstring& parameters, PreampCommand& out)
{
	out = PreampCommand();
	if (command != L"Preamp")
		return false;

	const std::wstring value = numeric_text::normalizeDecimalComma(parameters);
	double preampDb;
	const int matched = swscanf_s(value.c_str(), L" %lf dB", &preampDb);
	if (matched == 1 && std::isfinite(preampDb))
	{
		out.valid = true;
		out.dbGain = preampDb;
		out.noOp = std::abs(preampDb) < 1e-9;
	}
	return true;
}

std::wstring PreampCommand::serialize() const
{
	// %g matches QString::arg(double)'s default formatting (C locale, format 'g',
	// six significant digits with trailing zeros stripped). The "<value> dB" shape
	// is exactly what the factory parser reads back via swscanf_s(" %lf dB").
	wchar_t buffer[64];
	swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g dB", dbGain);
	return std::wstring(buffer);
}
