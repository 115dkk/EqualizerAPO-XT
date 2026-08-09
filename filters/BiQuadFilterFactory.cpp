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
#define _USE_MATH_DEFINES
#include <cmath>
#include <regex>
#include <sstream>

#include "runtime/memory/MemoryHelper.h"
#include "text/StringHelper.h"
#include "services/logging/LogHelper.h"
#include "BiQuadFilter.h"
#include "filters/FilterFactoryRegistry.h"
#include "BiQuadFilterFactory.h"

REGISTER_FILTER_FACTORY(FilterFactoryPriority::BiQuad, BiQuadFilterFactory, L"Filter")

using std::find;
using std::regex;
using std::string;
using std::vector;
using std::wregex;
using std::wsmatch;
using std::wstringstream;
using std::wstring;

static wregex regexType(L"^\\s*ON\\s+([A-Za-z]+)");
static wregex regexFreq(L"\\s+Fc\\s*([-+0-9.eE\u00A0]+)\\s*H\\s*z");
static wregex regexGain(L"\\s+Gain\\s*([-+0-9.eE]+)\\s*dB");
static wregex regexQ(L"\\s+Q\\s*([-+0-9.eE]+)");
static wregex regexBW(L"\\s+BW\\s+Oct\\s*([-+0-9.eE]+)");
static wregex regexSlope(L"^\\s*([-+0-9.eE]+)\\s*dB");
// The section order, currently only meaningful for an all-pass.
//
// A parameter rather than a new type keyword ("AP1"), because the type regex
// above accepts letters only: a digit in the keyword would be cut off the type
// token and drift into the parameters, where it would be read as part of
// something else. "Order" is also already the word this format uses for the
// same idea in "Filter: ON IIR Order <m> Coefficients ...", so the vocabulary
// does not grow.
static wregex regexOrder(L"\\s+Order\\s*([0-9]+)");

// Lowercase description used only in the parse trace/log messages. Kept local
// (and separate from the capitalized biquadTypeTitle the GUI shows) so the
// engine log output stays byte-identical to the previous code.
static const wchar_t* typeLogDescription(BiQuad::Type type)
{
	switch (type)
	{
	case BiQuad::PEAKING:
		return L"peaking";
	case BiQuad::LOW_PASS:
		return L"low-pass";
	case BiQuad::HIGH_PASS:
		return L"high-pass";
	case BiQuad::BAND_PASS:
		return L"band-pass";
	case BiQuad::LOW_SHELF:
		return L"low-shelf";
	case BiQuad::HIGH_SHELF:
		return L"high-shelf";
	case BiQuad::NOTCH:
		return L"notch";
	case BiQuad::ALL_PASS:
		return L"all-pass";
	case BiQuad::ALL_PASS_1:
		return L"1st-order all-pass";
	}
	return L"";
}

BiQuadFilterFactory::BiQuadFilterFactory()
{
}

bool BiQuadFilterFactory::parseCommand(const wstring& command, wstring& parameters, BiQuadCommand& out)
{
	// starts-with check (rfind at position 0), the pre-C++20 idiom
	if (command.rfind(L"Filter", 0) != 0)
		return false;

	// Conversion to period as decimal mark, if needed
	parameters = StringHelper::normalizeDecimalComma(parameters);

	wsmatch match;
	wstring typeString;

	bool found = regex_search(parameters, match, regexType);
	if (!found)
		return false;

	typeString = match.str(1);
	BiQuad::Type type;
	if (!biquadTypeFromName(typeString, type))
	{
		if (typeString != L"None")
			LogFStatic(L"Invalid filter type %s", typeString.c_str());
		return false;
	}

	const wchar_t* typeDescription = typeLogDescription(type);
	parameters = match.suffix().str();

	wstringstream stream;
	stream << L"Adding " << typeDescription << L" filter";

	double freq = 0;
	double gain = 0;
	double bandwidthOrQOrS = 0;
	bool isBandwidthOrS = false;
	bool isCornerFreq = false;
	bool orderWasExplicit = false;
	bool error = false;

	found = regex_search(parameters, match, regexFreq);
	if (found)
	{
		wstring freqString = match.str(1);
		freq = getFreq(freqString);
		stream << " with frequency " << freq << " Hz";
	}
	else
	{
		LogFStatic(L"No frequency given in filter string %s%s", typeString.c_str(), parameters.c_str());
		error = true;
	}

	found = regex_search(parameters, match, regexGain);
	if (found)
	{
		if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::NOTCH || type == BiQuad::ALL_PASS)
			TraceFStatic(L"Ignoring gain for filter of type %s", typeDescription);
		else
		{
			wstring gainString = match.str(1);
			gain = StringHelper::parseDouble(gainString);
			if (type == BiQuad::PEAKING)
				stream << ", gain " << gain << " dB";
			else
				stream << " and gain " << gain << " dB";
		}
	}
	else if (type == BiQuad::PEAKING || type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
	{
		LogFStatic(L"No gain given in filter string %s%s", typeString.c_str(), parameters.c_str());
		error = true;
	}

	found = regex_search(parameters, match, regexQ);
	if (found)
	{
		wstring qString = match.str(1);
		bandwidthOrQOrS = StringHelper::parseDouble(qString);
		stream << " and Q " << bandwidthOrQOrS;
	}

	found = regex_search(parameters, match, regexBW);
	if (found)
	{
		if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
			TraceFStatic(L"Ignoring bandwidth for filter of type %s", typeDescription);
		else
		{
			wstring bwString = match.str(1);
			bandwidthOrQOrS = StringHelper::parseDouble(bwString);
			isBandwidthOrS = true;
			stream << " and bandwidth " << bandwidthOrQOrS << " octaves";
		}
	}

	found = regex_search(parameters, match, regexSlope);
	if (found)
	{
		if (!(type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF))
			TraceFStatic(L"Ignoring slope for filter of type %s", typeDescription);
		else
		{
			wstring slopeString = match.str(1);
			bandwidthOrQOrS = StringHelper::parseDouble(slopeString);
			isBandwidthOrS = true;
			stream << " and slope " << bandwidthOrQOrS << " dB";
		}
	}

	found = regex_search(parameters, match, regexOrder);
	if (found)
	{
		if (type != BiQuad::ALL_PASS)
			TraceFStatic(L"Ignoring order for filter of type %s", typeDescription);
		else
		{
			const wstring orderString = match.str(1);
			const long order = wcstol(orderString.c_str(), nullptr, 10);
			if (order == 1)
			{
				type = BiQuad::ALL_PASS_1;
				typeDescription = typeLogDescription(type);
				stream << L" as a 1st-order section";
			}
			else if (order != 2)
			{
				LogFStatic(L"Order must be 1 or 2 in filter string %s%s", typeString.c_str(), parameters.c_str());
				error = true;
			}
			orderWasExplicit = true;
		}
	}

	if (type == BiQuad::ALL_PASS_1 && bandwidthOrQOrS != 0)
	{
		// A 1st-order section has no width: how fast its phase turns is fixed
		// by Fc alone. A Q or bandwidth on such a line is not an error - a user
		// who switches an existing filter to 1st order leaves one behind - but
		// it is discarded, and said so, the way a gain on an all-pass is.
		TraceFStatic(L"Ignoring width for filter of type %s", typeDescription);
		bandwidthOrQOrS = 0;
		isBandwidthOrS = false;
	}

	if (!std::isfinite(freq) || !std::isfinite(gain) || !std::isfinite(bandwidthOrQOrS))
	{
		LogFStatic(L"Filter parameters must be finite in filter string %s%s",
			typeString.c_str(), parameters.c_str());
		error = true;
	}

	if (bandwidthOrQOrS == 0)
	{
		if (type == BiQuad::PEAKING || type == BiQuad::ALL_PASS)
		{
			LogFStatic(L"No Q or bandwidth given in filter string %s%s", typeString.c_str(), parameters.c_str());
			error = true;
		}
		else if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::BAND_PASS)
		{
			bandwidthOrQOrS = M_SQRT1_2;
		}
		else if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
		{
			bandwidthOrQOrS = 0.9; // found out by experimentation with RoomEQWizard
			isBandwidthOrS = true;
		}
		else if (type == BiQuad::NOTCH)
		{
			bandwidthOrQOrS = 30.0; // found out by experimentation with RoomEQWizard
		}
	}
	else if (type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
	{
		if (isBandwidthOrS)
			// Maximum S is 1 for 12 dB
			bandwidthOrQOrS /= 12.0;
		if (typeString[typeString.length() - 1] != L'C')
			isCornerFreq = true;
	}

	if (error)
		return false;

	TraceFStatic(L"%s", stream.str().c_str());

	out.type = type;
	out.dbGain = gain;
	out.freq = freq;
	out.bandwidthOrQOrS = bandwidthOrQOrS;
	out.isBandwidthOrS = isBandwidthOrS;
	out.isCornerFreq = isCornerFreq;
	out.orderWasExplicit = orderWasExplicit;
	out.enabled = true;
	return true;
}

FilterVector BiQuadFilterFactory::createFilter(const wstring& configPath, wstring& command, wstring& parameters)
{
	BiQuadCommand cmd;
	if (!parseCommand(command, parameters, cmd))
		return {};

	return singleFilter(makeFilter<BiQuadFilter>(
		cmd.type, cmd.dbGain, cmd.freq, cmd.bandwidthOrQOrS, cmd.isBandwidthOrS, cmd.isCornerFreq));
}

double BiQuadFilterFactory::getFreq(const wstring& freqString)
{
	double result;
	// remove thousand's separator for locales utilizing non-breaking space
	wstring s = StringHelper::replaceCharacters(freqString, L"\u00A0", L"");
	int matched = swscanf_s(s.c_str(), L"%lf", &result);
	if (matched == 1)
	{
		if (s.length() >= 5 && s.find_first_of(L"eE") == wstring::npos)
		{
			if (s[s.length() - 4] == L'.')
			{
				// Interpret as thousands separator because of Room EQ Wizard
				result *= 1000.0;
			}
		}

		return result;
	}
	else
		return -1.0;
}
