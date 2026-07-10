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

#include "DelayCommand.h"

#include <cstdio>

std::wstring DelayCommand::serialize() const
{
	// Format the magnitude with the C "%g" default (six significant digits, no
	// trailing zeros). For every value the Delay GUI can produce (integer
	// samples in [1, 10000] and millisecond values with at most two decimals in
	// [0, 10000]) this matches the QString("%0").arg(double) text.
	wchar_t buffer[64];
	swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g", delay);

	std::wstring result(buffer);
	result += isMs ? L" ms" : L" samples";
	return result;
}
