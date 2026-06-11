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

// Single owner of the "Include:" config-line grammar, shared by the engine
// factory and the Editor GUI.
struct IncludeCommand
{
	// Include path as the author wrote it, with only leading whitespace
	// stripped. Everything after the first non-blank character is part of the
	// path (the engine passes it to the file system verbatim); relative paths
	// are resolved against the including file by the consumers.
	std::wstring path;

	// Canonical parameter string: the path itself.
	std::wstring serialize() const;

	// Returns true when command names an Include line; path is then the
	// parameter text without leading whitespace and may be empty. Emptiness
	// policy stays with the caller.
	static bool parse(const std::wstring& command, const std::wstring& parameters, IncludeCommand& out);
};
