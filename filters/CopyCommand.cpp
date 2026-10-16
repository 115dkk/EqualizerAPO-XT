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

#include "CopyCommand.h"

#include <cmath>
#include <cstdio>

#include "audio/ChannelLayout.h"
#include "parser/NumericText.h"
#include "services/logging/Logging.h"
#include "text/WideString.h"

using std::pow;
using std::vector;
using std::wstring;

std::vector<Assignment> parseCopyAssignments(const wstring& parameters)
{
	// One parse shared by the engine factory and the Editor GUI factory; the
	// copy_crossfeed regression reference pins this exact grammar.
	vector<Assignment> assignments;

	vector<wstring> assignmentStrings = text::split(parameters, L' ');
	for (vector<wstring>::iterator it = assignmentStrings.begin(); it != assignmentStrings.end(); it++)
	{
		Assignment assignment;

		vector<wstring> parts = text::split(*it, L'=');
		if (parts.size() == 2)
		{
			wstring target = parts[0];
			wstring source = parts[1];

			assignment.targetChannel = target;

			vector<wstring> summands = text::split(source, '+');
			bool validAssignment = true;
			for (vector<wstring>::iterator it2 = summands.begin(); it2 != summands.end(); it2++)
			{
				vector<wstring> factors = text::split(*it2, '*');
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
					// Audit #250 F015 and #348 TD-42: the decimal comma and the C
					// locale come with numeric_text; a text that starts with no
					// number is dropped like a non-finite one instead of reading
					// as 0.
					const numeric_text::Number number = numeric_text::readNumber(factor);
					summand.factor = number.value;
					summand.isDecibel = factor.size() > 2 && text::toLower(factor.substr(factor.size() - 2)) == L"db";
					const double linearFactor = summand.isDecibel
						? pow(10.0, summand.factor / 20.0) : summand.factor;
					if (number.length == 0 || !std::isfinite(linearFactor))
					{
						LogFStatic(L"Copy factor %s for target %s must be a finite number; ignoring assignment",
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

		ChannelLayout::declare(channelNames, assignment.targetChannel);
	}
}
