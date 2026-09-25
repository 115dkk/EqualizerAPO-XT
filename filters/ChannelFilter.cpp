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
#include <sstream>
#include <algorithm>

#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"
#include "ChannelCommand.h"
#include "ChannelFilter.h"
#include "diagnostics/performance/PerfProfile.h"

using std::vector;
using std::wstringstream;
using std::wstring;

ChannelFilter::ChannelFilter(const vector<wstring>& words)
	: words(words)
{
}

ChannelFilter::~ChannelFilter()
{
}

vector<wstring> ChannelFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	// The codec's selection, the one the Editor mirrors (audit #348 A2); the
	// engine logs unknown selectors, once per load.
	vector<wstring> selectedChannelNames;
	wstringstream channelNumbers;
	for (const size_t c : ChannelCommand::selectedIndices(words, channelNames, true))
	{
		selectedChannelNames.push_back(channelNames[c]);
		if (channelNumbers.tellp() > 0)
			channelNumbers << L", ";
		channelNumbers << c + 1;
	}

	TraceF(L"Selecting channel(s) number %s", channelNumbers.str().c_str());

	return selectedChannelNames;
}

#pragma AVRT_CODE_BEGIN
void ChannelFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("ChannelFilter::process");
	// nothing to do
}
#pragma AVRT_CODE_END
