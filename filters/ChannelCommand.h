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

// Single owner of the "Channel:" config-line grammar, shared by the engine
// factory and the Editor GUI. Channel selectors are upper-cased names or
// position numbers separated by whitespace and/or commas.
struct ChannelCommand
{
	// Upper-cased selector tokens in the order they were written. May be empty:
	// "Channel:" with no selectors is a valid line that selects no channel.
	std::vector<std::wstring> channels;

	// Canonical space-separated parameter string.
	std::wstring serialize() const;

	// Returns true when command names a Channel line; channels is then filled
	// from parameters. Emptiness policy stays with the caller.
	static bool parse(const std::wstring& command, const std::wstring& parameters, ChannelCommand& out);

	// The selection ChannelFilter::initialize would produce for these
	// selector tokens over channelNames: a subset of channelNames IN
	// channelNames ORDER (never the written order), ALL selecting
	// everything, unknown selectors ignored. Kept equivalent to the filter
	// by ChannelCommandTests; the Editor mirrors the engine's selection
	// flow through this one routine.
	static std::vector<std::wstring> resolveSelection(const std::vector<std::wstring>& selectorTokens,
		const std::vector<std::wstring>& channelNames);
};
