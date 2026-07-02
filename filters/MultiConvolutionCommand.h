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

// Single owner of the "MultiConvolution:" config-line grammar, shared by the
// engine factory and the Editor GUI. The line names one output channel followed
// by a single multi-channel impulse response path:
//
//   MultiConvolution: <outputChannel> <impulse response path>
//
// The output channel is the first whitespace-delimited token; everything after
// the first space (trimmed) is the path, so paths may contain inner spaces just
// like the "Convolution:" grammar. Quotes and environment variables are left in
// the path for ConvolutionFilePath::resolve to handle.
struct MultiConvolutionCommand
{
	std::wstring outputChannel;
	std::wstring path;

	// Canonical parameter string: "<outputChannel> <path>".
	const std::wstring& serialize() const;

	// Returns true only when command is "MultiConvolution" and the parameters
	// carry both an output channel and a non-empty path. A line with just a
	// channel (no path) is rejected, unlike the single-field Convolution grammar.
	static bool parse(const std::wstring& command, const std::wstring& parameters, MultiConvolutionCommand& out);

private:
	// serialize() returns a reference, so the composed string is cached here.
	mutable std::wstring serialized;
};
