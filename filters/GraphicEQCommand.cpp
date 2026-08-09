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

#include "stdafx.h"
#include "text/WideString.h"
#include "parser/NumericText.h"

#include "GraphicEQCommand.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>

#include "services/logging/Logging.h"

using std::sort;
using std::wregex;
using std::wsmatch;
using std::wsregex_iterator;
using std::wstring;

// Kept as a static so the (relatively expensive) std::wregex is compiled once.
static wregex regexNumber(L"[-+0-9.eE]+");

void GraphicEQCommand::parse(const wstring& parameters)
{
	nodes.clear();

	// The graphiceq_15band regression reference pins this parse. When no period
	// is present the parameter is assumed to use a comma decimal mark and the
	// commas are turned into periods first.
	wstring value = parameters;
	if (value.find(L'.') == wstring::npos)
		value = text::replaceCharacters(value, L",", L".");

	wsregex_iterator end;

	// Consume freq/gain pairs; a trailing unpaired number is dropped. The loop
	// advances only via the body's *it++, so it never increments a past-the-end
	// iterator (combining a for-loop increment with *it++ would step past end on
	// an odd token count — undefined behavior).
	for (wsregex_iterator it(value.begin(), value.end(), regexNumber); it != end; )
	{
		wsmatch freqMatch = *it++;
		if (it == end)
			break;
		wsmatch gainMatch = *it++;
		double freq = numeric_text::parseDouble(freqMatch.str(0));
		double gain = numeric_text::parseDouble(gainMatch.str(0));
		if (!std::isfinite(freq) || !std::isfinite(gain))
		{
			LogFStatic(L"GraphicEQ frequency and gain must be finite; ignoring pair %s %s",
				freqMatch.str(0).c_str(), gainMatch.str(0).c_str());
			continue;
		}
		FilterNode node(freq, gain);
		nodes.push_back(node);
	}
	sort(nodes.begin(), nodes.end());
}

wstring GraphicEQCommand::serialize() const
{
	// Each freq and gain is formatted with the C "%g" default (six significant
	// digits, trailing zeros stripped), matching QString::arg(double); the two
	// values are separated by a single space, and pairs are joined with "; ".
	wstring result;
	bool first = true;
	for (const FilterNode& node : nodes)
	{
		if (first)
			first = false;
		else
			result += L"; ";

		wchar_t buffer[64];
		swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g %g", node.freq, node.dbGain);
		result += buffer;
	}
	return result;
}
