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

// Single owner of the "Device:" config-line grammar and its matching rules,
// shared by the engine factory and the Editor GUI. The parameter string is a
// list of patterns separated by ';'; each pattern is a list of words separated
// by spaces. A device matches when every word of any one pattern occurs in its
// device string (case-insensitive); the single word "all" matches every device.
struct DeviceCommand
{
	// Word lists per pattern, in the order they were written. Empty patterns
	// (";;") contribute nothing and are dropped.
	std::vector<std::vector<std::wstring>> patterns;

	// True when deviceString satisfies this pattern list. Words are matched as
	// case-insensitive substrings; a word without '{' is matched against the
	// device string with GUIDs removed, so plain words cannot accidentally hit
	// a GUID fragment. An empty pattern list matches nothing.
	bool matches(const std::wstring& deviceString) const;

	// Canonical parameter string: words joined with spaces, patterns joined
	// with "; ".
	std::wstring serialize() const;

	// Tokenizes a raw pattern string (the parameter text of a "Device:" line)
	// into a pattern list. Shared with callers that hold a pattern outside a
	// config line, like the Editor's device dialog.
	static DeviceCommand fromPattern(const std::wstring& pattern);

	// Returns true when command names a Device line; patterns is then filled
	// from parameters.
	static bool parse(const std::wstring& command, const std::wstring& parameters, DeviceCommand& out);
};
