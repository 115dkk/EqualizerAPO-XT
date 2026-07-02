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

#include "IncludeCommand.h"

#include <cwctype>

using std::wstring;

const wstring& IncludeCommand::serialize() const
{
	return path;
}

bool IncludeCommand::parse(const wstring& command, const wstring& parameters, IncludeCommand& out)
{
	if (command != L"Include")
		return false;

	// Leading-whitespace strip preserved from the engine factory. Trailing
	// characters are kept: they belong to the path as written.
	size_t start = 0;
	while (start < parameters.length() && iswspace(parameters[start]))
		start++;
	out.path = parameters.substr(start);

	return true;
}
