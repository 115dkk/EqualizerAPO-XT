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

// Single owner of the "Convolution:" config-line grammar, shared by the engine
// factory and the Editor GUI.
struct ConvolutionCommand
{
	// Impulse response path as the author wrote it (whitespace-trimmed only).
	// Quotes and environment variables are preserved so serialize() round-trips
	// the config text; ConvolutionFilePath::resolve applies unquoting, variable
	// expansion, and config-relative resolution when the engine loads the file.
	std::wstring path;

	// Canonical parameter string: the path itself.
	std::wstring serialize() const;

	// Returns true when command names a Convolution line; path is then the
	// trimmed parameter text and may be empty. Emptiness policy stays with the
	// caller (the engine builds no filter for an empty path; the Editor still
	// opens the GUI so the user can pick a file).
	static bool parse(const std::wstring& command, const std::wstring& parameters, ConvolutionCommand& out);
};
