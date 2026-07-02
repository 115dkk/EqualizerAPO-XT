/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

// Single owner of the "MultiConvolution:" config-line grammar, shared by the
// engine factory and the Editor GUI. Two forms are accepted:
//
//   MultiConvolution: <target>=<ir ch>[+<ir ch>...] [<target>=... ...] <path>
//   MultiConvolution: <target> <path>                        (simple form)
//
// Each mapping names one output channel and the 0-based channels of the single
// multi-channel impulse-response file that are convolved with that output
// channel's own (pre-command) signal and summed into it. The simple form means
// "every channel of the file" and is represented by one mapping with an empty
// irChannels list; the file's channel count is only known when the file is
// read, so the expansion happens in the filter.
//
// Whitespace around '=' and '+' is accepted ("L = 0 + 1" and "L=0+1" parse
// identically); serialize() writes the compact form. The path is the raw
// remainder of the line and keeps its inner spaces: the first word that cannot
// continue the mapping grammar starts the path, so file names containing '='
// or '+' still work. Quotes and environment variables are left in the path for
// ConvolutionFilePath::resolve to handle.
struct MultiConvolutionCommand
{
	struct Mapping
	{
		std::wstring targetChannel;
		// 0-based channel indices into the impulse-response file. Empty means
		// every channel of the file (the simple form).
		std::vector<unsigned> irChannels;
	};

	std::vector<Mapping> mappings;
	std::wstring path;

	// True when the line was (or will be) written as "<target> <path>".
	bool isSimpleForm() const;

	// Canonical parameter string: "<target> <path>" for the simple form,
	// "<t>=<i>+<i> ... <path>" otherwise.
	const std::wstring& serialize() const;

	// Returns true only when command is "MultiConvolution" and the parameters
	// carry at least one mapping and a non-empty path.
	static bool parse(const std::wstring& command, const std::wstring& parameters, MultiConvolutionCommand& out);

private:
	// serialize() returns a reference, so the composed string is cached here.
	mutable std::wstring serialized;
};
