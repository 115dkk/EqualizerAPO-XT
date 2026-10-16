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
#include <algorithm>
#include <cmath>
#include <sstream>

#include "services/logging/Logging.h"
#include "runtime/memory/AlignedMemory.h"
#include "audio/ChannelLayout.h"
#include "CopyFilter.h"
#include "diagnostics/performance/PerfProfile.h"

using std::find;
using std::pow;
using std::vector;
using std::wstringstream;
using std::wstring;

CopyFilter::CopyFilter(const vector<Assignment>& assignments)
	: assignments(assignments)
{
}

vector<wstring> CopyFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	inputChannelCount = channelNames.size();
	hasNonzeroConstant = false;
	vector<InternalAssignment> preparedAssignments;
	preparedAssignments.reserve(assignments.size());
	vector<wstring> outChannelNames;

	for (const Assignment& a : assignments)
	{
		const wstring channelName = ChannelLayout::resolveTarget(a.targetChannel, channelNames).name;
		vector<wstring>::const_iterator it = find(outChannelNames.begin(), outChannelNames.end(), channelName);
		const int targetChannel = static_cast<int>(it - outChannelNames.begin());
		if (it == outChannelNames.end())
			outChannelNames.push_back(channelName);

		vector<InternalSummand> sourceSum;
		sourceSum.reserve(a.sourceSum.size());

		for (const Assignment::Summand& s : a.sourceSum)
		{
			int sourceChannel;
			if (s.channel != L"")
				sourceChannel = ChannelLayout::getChannelIndex(s.channel, channelNames);
			else
				sourceChannel = -1;

			double factor;
			if (s.isDecibel)
				factor = pow(10.0, s.factor / 20.0);
			else
				factor = s.factor;

			if (sourceChannel == -1 && factor != 0.0)
				hasNonzeroConstant = true;
			sourceSum.emplace_back(sourceChannel, factor);
		}

		preparedAssignments.emplace_back(targetChannel, std::move(sourceSum));
	}

	wstringstream stream;
	stream << "Copying ";
	for (size_t i = 0; i < preparedAssignments.size(); i++)
	{
		InternalAssignment& ia = preparedAssignments[i];
		if (i > 0)
			stream << ", ";
		stream << L"to channel " << outChannelNames[ia.targetChannel].c_str() << " ";
		for (size_t j = 0; j < ia.sourceSum.size(); j++)
		{
			const InternalSummand& is = ia.sourceSum[j];
			if (j > 0)
				stream << ", ";
			if (is.channel != -1)
				stream << L"from channel " << channelNames[is.channel].c_str() << L" with factor " << is.factor;
			else
				stream << L"value " << is.factor;
		}
	}
	TraceF(L"%s", stream.str().c_str());
	internalAssignments = std::move(preparedAssignments);

	return outChannelNames;
}

#pragma AVRT_CODE_BEGIN
void CopyFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("CopyFilter::process");
	if (hasNonzeroConstant)
	{
		bool silent = true;
		for (size_t channel = 0; channel < inputChannelCount && silent; channel++)
		{
			for (unsigned frame = 0; frame < frameCount; frame++)
			{
				if (input[channel][frame] != 0.0)
				{
					silent = false;
					break;
				}
			}
		}
		if (silent)
		{
			for (const InternalAssignment& assignment : internalAssignments)
				if (assignment.targetChannel != -1)
					std::fill_n(output[assignment.targetChannel], frameCount, 0.0);
			return;
		}
	}

	for (const InternalAssignment& ia : internalAssignments)
	{
		if (ia.targetChannel == -1 || ia.sourceSum.empty())
			continue;

		{
			const InternalSummand& is = ia.sourceSum[0];

			if (is.channel == -1)
				for (unsigned f = 0; f < frameCount; f++)
					output[ia.targetChannel][f] = is.factor;
			else if (is.factor == 1.0)
				std::copy_n(input[is.channel], frameCount, output[ia.targetChannel]);
			else
				for (unsigned f = 0; f < frameCount; f++)
					output[ia.targetChannel][f] = is.factor * input[is.channel][f];
		}

		for (size_t j = 1; j < ia.sourceSum.size(); j++)
		{
			const InternalSummand& is = ia.sourceSum[j];

			if (is.channel == -1)
				for (unsigned f = 0; f < frameCount; f++)
					output[ia.targetChannel][f] += is.factor;
			else if (is.factor == 1.0)
				for (unsigned f = 0; f < frameCount; f++)
					output[ia.targetChannel][f] += input[is.channel][f];
			else
				for (unsigned f = 0; f < frameCount; f++)
					output[ia.targetChannel][f] += is.factor * input[is.channel][f];
		}
	}
}
#pragma AVRT_CODE_END
