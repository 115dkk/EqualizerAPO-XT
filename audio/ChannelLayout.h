/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

class ChannelLayout
{
public:
	static int getDefaultChannelMask(int channelCount);
	static std::vector<std::wstring> getChannelNames(int channelCount, int channelMask);
	// The position of a channel named by word: a name, an alias (SL/RL,
	// SR/RR, SUB for LFE) or a 1-based number. -1 when it names none.
	// allowAdditional: the caller handles a word that names no channel
	// itself (Copy and MultiConvolution declare it as a new channel, the
	// Editor resolves on every propagation), so nothing is logged for it.
	static int getChannelIndex(std::wstring word, const std::vector<std::wstring>& channelNames, bool allowAdditional = false);

	// A channel a Copy or MultiConvolution line writes (audit #348 A2): an
	// existing channel by name, alias or number, or else a new virtual
	// channel named as written. index is the existing channel's position in
	// channelNames, -1 for a new one. The filters declare their outputs by
	// this rule and the Editor mirrors them with declare().
	struct Target
	{
		std::wstring name;
		int index;
	};
	static Target resolveTarget(const std::wstring& word, const std::vector<std::wstring>& channelNames);

	// Adds a target to a configuration's channel list the way
	// FilterEngine::addFilters adds a filter's output channel: appended
	// once, by name.
	static void declare(std::vector<std::wstring>& channelNames, const std::wstring& word);

	// The layout the Editor's analysis runs with for a device and a selected
	// channel configuration: the device's own channel count when nothing
	// else is selected (mask 0 or the device's mask), the selected mask's
	// bit count otherwise, and 7.1 when that comes out as zero. The analysis
	// channel list and the analysis thread both read this one rule; they
	// used to carry a copy each, and the thread indexes its buffer with the
	// list's position (audit #348 TD-20).
	struct AnalysisLayout
	{
		unsigned channelCount;
		int channelMask;
	};
	static AnalysisLayout analysisLayout(unsigned deviceChannelCount, unsigned deviceChannelMask, int selectedMask);
};
