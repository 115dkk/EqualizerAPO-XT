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

#include <cstdio>

#include "services/logging/LogHelper.h"
#include "runtime/memory/MemoryHelper.h"
#include "audio/ChannelHelper.h"
#include "text/StringHelper.h"
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
		wstring channelName = a.targetChannel;
		int channelIndex = ChannelHelper::getChannelIndex(a.targetChannel, channelNames, true);
		if (channelIndex != -1)
			channelName = channelNames[channelIndex];
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
				sourceChannel = ChannelHelper::getChannelIndex(s.channel, channelNames);
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

const std::vector<Assignment>& CopyFilter::getAssignments() const
{
	return assignments;
}

std::vector<Assignment> parseCopyAssignments(const wstring& parameters)
{
	// One parse shared by the engine factory and the Editor GUI factory; the
	// copy_crossfeed regression reference pins this exact grammar.
	vector<Assignment> assignments;

	vector<wstring> assignmentStrings = StringHelper::split(parameters, L' ');
	for (vector<wstring>::iterator it = assignmentStrings.begin(); it != assignmentStrings.end(); it++)
	{
		Assignment assignment;

		vector<wstring> parts = StringHelper::split(*it, L'=');
		if (parts.size() == 2)
		{
			wstring target = parts[0];
			wstring source = parts[1];

			assignment.targetChannel = target;

			vector<wstring> summands = StringHelper::split(source, '+');
			bool validAssignment = true;
			for (vector<wstring>::iterator it2 = summands.begin(); it2 != summands.end(); it2++)
			{
				vector<wstring> factors = StringHelper::split(*it2, '*');
				wstring factor;
				wstring channel;
				if (factors.size() == 2)
				{
					factor = factors[0];
					channel = factors[1];
				}
				else if (factors.size() == 1)
				{
					if (factors[0] == L"0" || factors[0].find(L'.') != wstring::npos)
						factor = factors[0];
					else
						channel = factors[0];
				}

				Assignment::Summand summand;
				if (factor == L"")
				{
					summand.factor = 1.0;
					summand.isDecibel = false;
				}
				else
				{
					// Audit #250 F015: BiQuad/Preamp/Delay normalize the decimal
					// comma; a raw wcstod here silently truncated "0,5" to 0.
					summand.factor = wcstod(StringHelper::normalizeDecimalComma(factor).c_str(), nullptr);
					summand.isDecibel = factor.size() > 2 && StringHelper::toLowerCase(factor.substr(factor.size() - 2)) == L"db";
					const double linearFactor = summand.isDecibel
						? pow(10.0, summand.factor / 20.0) : summand.factor;
					if (!std::isfinite(summand.factor) || !std::isfinite(linearFactor))
					{
						LogFStatic(L"Copy factor %s for target %s must be finite; ignoring assignment",
							factor.c_str(), target.c_str());
						validAssignment = false;
						break;
					}
				}

				summand.channel = channel;
				assignment.sourceSum.push_back(summand);
			}
			if (!validAssignment)
				assignment.sourceSum.clear();
		}

		if (assignment.targetChannel != L"" && !assignment.sourceSum.empty())
			assignments.push_back(assignment);
	}

	return assignments;
}

std::wstring serializeCopyAssignments(const vector<Assignment>& assignments)
{
	// Keeps a parse -> serialize round trip lossless. Each factor is
	// formatted with the C "%g" default (matching QString::setNum(double)); a bare
	// integer factor gains a ".0" suffix so it is recognised as a factor (not a
	// channel) on the next parse, and the dB suffix is appended for decibel
	// factors. A summand whose channel is a single space is the GUI's "not yet
	// filled row" sentinel and is skipped here.
	wstring result;
	bool firstAssignment = true;

	for (const Assignment& assignment : assignments)
	{
		if (assignment.targetChannel == L"")
			continue;

		bool firstSummand = true;
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			if (summand.channel == L" ")
				continue;

			if (firstSummand)
			{
				firstSummand = false;

				if (firstAssignment)
					firstAssignment = false;
				else
					result += L" ";

				result += assignment.targetChannel;
				result += L"=";
			}
			else
			{
				result += L"+";
			}

			bool hasChannel = summand.channel != L"";
			bool hasFactor = !hasChannel || summand.factor != 1.0 || summand.isDecibel;

			if (hasFactor)
			{
				// QString::setNum(double) uses the C "%g" default (six significant
				// digits, trailing zeros stripped); std::swprintf with "%g" produces
				// the same text in the C locale.
				wchar_t buffer[64];
				swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g", summand.factor);
				wstring factorString(buffer);
				if (factorString != L"0" && factorString.find(L'.') == wstring::npos)
					factorString += L".0";
				result += factorString;
				if (summand.isDecibel)
					result += L"dB";
			}

			if (hasFactor && hasChannel)
				result += L"*";

			if (hasChannel)
				result += summand.channel;
		}
	}

	return result;
}


void propagateCopyChannels(const vector<Assignment>& assignments, vector<wstring>& channelNames)
{
	for (const Assignment& assignment : assignments)
	{
		if (assignment.targetChannel.empty())
			continue;

		bool hasSummand = false;
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			// A single space is the legacy form's unfinished-row sentinel.
			// An empty channel is a constant summand and therefore still
			// produces the target channel.
			if (summand.channel != L" ")
			{
				hasSummand = true;
				break;
			}
		}
		if (!hasSummand)
			continue;

		if (ChannelHelper::getChannelIndex(assignment.targetChannel, channelNames, true) == -1)
			channelNames.push_back(assignment.targetChannel);
	}
}
