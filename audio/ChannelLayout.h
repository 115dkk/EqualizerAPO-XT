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
	static int getChannelIndex(std::wstring word, const std::vector<std::wstring>& channelNames, bool allowAdditional = false);

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
