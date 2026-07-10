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
#include <sstream>

#include <cstdio>

#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/ChannelHelper.h"
#include "helpers/StringHelper.h"
#include "CopyFilter.h"
#include "helpers/PerfProfile.h"

using std::find;
using std::pow;
using std::vector;
using std::wstringstream;
using std::wstring;

CopyFilter::CopyFilter(const vector<Assignment>& assignments)
{
	this->assignments = assignments;

	internalAssignments = nullptr;
	assignmentCount = 0;
}

CopyFilter::~CopyFilter()
{
	cleanup();
}

vector<wstring> CopyFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();

	assignmentCount = (unsigned)assignments.size();
	internalAssignments = (InternalAssignment*)MemoryHelper::alloc(assignmentCount * sizeof(InternalAssignment));

	vector<wstring> outChannelNames;

	for (unsigned i = 0; i < assignmentCount; i++)
	{
		InternalAssignment& ia = internalAssignments[i];
		Assignment& a = assignments[i];

		wstring channelName = a.targetChannel;
		int channelIndex = ChannelHelper::getChannelIndex(a.targetChannel, channelNames, true);
		if (channelIndex != -1)
			channelName = channelNames[channelIndex];
		vector<wstring>::const_iterator it = find(outChannelNames.begin(), outChannelNames.end(), channelName);
		ia.targetChannel = static_cast<int>(it - outChannelNames.begin());
		if (it == outChannelNames.end())
			outChannelNames.push_back(channelName);

		ia.sourceCount = (unsigned)a.sourceSum.size();
		ia.sourceSum = (InternalAssignment::InternalSummand*)MemoryHelper::alloc(ia.sourceCount * sizeof(InternalAssignment::InternalSummand));

		for (unsigned j = 0; j < ia.sourceCount; j++)
		{
			InternalAssignment::InternalSummand& is = ia.sourceSum[j];
			Assignment::Summand& s = a.sourceSum[j];

			if (s.channel != L"")
				is.channel = ChannelHelper::getChannelIndex(s.channel, channelNames);
			else
				is.channel = -1;

			if (s.isDecibel)
				is.factor = pow(10.0, s.factor / 20.0);
			else
				is.factor = s.factor;
		}
	}

	wstringstream stream;
	stream << "Copying ";
	for (unsigned i = 0; i < assignmentCount; i++)
	{
		InternalAssignment& ia = internalAssignments[i];
		if (i > 0)
			stream << ", ";
		stream << L"to channel " << outChannelNames[ia.targetChannel].c_str() << " ";
		for (unsigned j = 0; j < ia.sourceCount; j++)
		{
			const InternalAssignment::InternalSummand& is = ia.sourceSum[j];
			if (j > 0)
				stream << ", ";
			if (is.channel != -1)
				stream << L"from channel " << channelNames[is.channel].c_str() << L" with factor " << is.factor;
			else
				stream << L"value " << is.factor;
		}
	}
	TraceF(L"%s", stream.str().c_str());

	return outChannelNames;
}

#pragma AVRT_CODE_BEGIN
void CopyFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("CopyFilter::process");
	for (unsigned i = 0; i < assignmentCount; i++)
	{
		InternalAssignment& ia = internalAssignments[i];

		if (ia.targetChannel == -1 || ia.sourceCount == 0)
			continue;

		{
			InternalAssignment::InternalSummand& is = ia.sourceSum[0];

			if (is.channel == -1)
				for (unsigned f = 0; f < frameCount; f++)
					output[ia.targetChannel][f] = is.factor;
			else if (is.factor == 1.0)
				std::copy_n(input[is.channel], frameCount, output[ia.targetChannel]);
			else
				for (unsigned f = 0; f < frameCount; f++)
					output[ia.targetChannel][f] = is.factor * input[is.channel][f];
		}

		for (unsigned j = 1; j < ia.sourceCount; j++)
		{
			const InternalAssignment::InternalSummand& is = ia.sourceSum[j];

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

void CopyFilter::cleanup()
{
	if (internalAssignments != nullptr)
	{
		for (unsigned i = 0; i < assignmentCount; i++)
		{
			InternalAssignment& ia = internalAssignments[i];
			if (ia.sourceSum != nullptr)
			{
				MemoryHelper::free(ia.sourceSum);
				ia.sourceSum = nullptr;
			}
		}

		MemoryHelper::free(internalAssignments);
		internalAssignments = nullptr;
	}
}

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
					summand.factor = wcstod(factor.c_str(), nullptr);
					summand.isDecibel = factor.size() > 2 && StringHelper::toLowerCase(factor.substr(factor.size() - 2)) == L"db";
				}

				summand.channel = channel;
				assignment.sourceSum.push_back(summand);
			}
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
