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

#include "BiQuadCommand.h"

#include <cmath>
#include <numbers>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "parser/NumericText.h"
#include "services/logging/Logging.h"
#include "text/WideString.h"


using std::wregex;
using std::wsmatch;
using std::wstringstream;
using std::wstring;

bool biquadTypeFromName(const std::wstring& name, BiQuad::Type& outType)
{
	// Single owner of the config keyword -> BiQuad type vocabulary.
	static const std::unordered_map<std::wstring, BiQuad::Type> nameToType = {
		{L"PK", BiQuad::PEAKING},
		{L"PEQ", BiQuad::PEAKING},
		{L"Modal", BiQuad::PEAKING},
		{L"LP", BiQuad::LOW_PASS},
		{L"HP", BiQuad::HIGH_PASS},
		{L"LPQ", BiQuad::LOW_PASS},
		{L"HPQ", BiQuad::HIGH_PASS},
		{L"BP", BiQuad::BAND_PASS},
		{L"LS", BiQuad::LOW_SHELF},
		{L"HS", BiQuad::HIGH_SHELF},
		{L"LSC", BiQuad::LOW_SHELF},
		{L"HSC", BiQuad::HIGH_SHELF},
		{L"NO", BiQuad::NOTCH},
		{L"AP", BiQuad::ALL_PASS},
	};

	auto it = nameToType.find(name);
	if (it == nameToType.end())
		return false;
	outType = it->second;
	return true;
}

const wchar_t* biquadTypeTitle(BiQuad::Type type)
{
	switch (type)
	{
	case BiQuad::PEAKING:
		return L"Peaking";
	case BiQuad::LOW_PASS:
		return L"Low-pass";
	case BiQuad::HIGH_PASS:
		return L"High-pass";
	case BiQuad::BAND_PASS:
		return L"Band-pass";
	case BiQuad::LOW_SHELF:
		return L"Low-shelf";
	case BiQuad::HIGH_SHELF:
		return L"High-shelf";
	case BiQuad::NOTCH:
		return L"Notch";
	case BiQuad::ALL_PASS:
		return L"All-pass";
	case BiQuad::ALL_PASS_1:
		return L"All-pass (1st order)";
	}
	return L"Biquad";
}


namespace
{
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

double getFreq(const wstring& freqString)
{
	double result;
	// remove thousand's separator for locales utilizing non-breaking space
	wstring s = text::replaceCharacters(freqString, L"\u00A0", L"");
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
}

bool BiQuadCommand::parse(const wstring& command, wstring& parameters,
	BiQuadCommand& out, wstring& error)
{
	error.clear();
	static const wregex regexType(L"^\\s*ON\\s+([A-Za-z]+)");
	static const wregex regexFreq(L"\\s+Fc\\s*([-+0-9.eE\u00A0]+)\\s*H\\s*z");
	static const wregex regexGain(L"\\s+Gain\\s*([-+0-9.eE]+)\\s*dB");
	static const wregex regexQ(L"\\s+Q\\s*([-+0-9.eE]+)");
	static const wregex regexBW(L"\\s+BW\\s+Oct\\s*([-+0-9.eE]+)");
	static const wregex regexSlope(L"^\\s*([-+0-9.eE]+)\\s*dB");
	static const wregex regexOrder(L"\\s+Order\\s*([0-9]+)");

	// starts-with check (rfind at position 0), the pre-C++20 idiom
	if (!command.starts_with(L"Filter"))
		return false;

	// Collect every reason for a recognized-but-malformed line. The factory
	// returns the completed reason to the engine's per-line trace.
	wstring reason;
	auto noteError = [&reason](const wstring& text) {
		if (!reason.empty())
			reason += L"; ";
		reason += text;
	};

	// Conversion to period as decimal mark, if needed
	parameters = numeric_text::normalizeDecimalComma(parameters);

	wsmatch match;
	wstring typeString;

	bool found = regex_search(parameters, match, regexType);
	if (!found)
		return false;

	typeString = match.str(1);
	BiQuad::Type type;
	if (!biquadTypeFromName(typeString, type))
	{
		// "None" is a valid no-op line. "IIR" belongs to IIRFilterFactory,
		// which runs at higher priority and reports its own parse errors, so
		// a broken IIR line must not earn a second report here.
		if (typeString == L"None" || typeString == L"IIR")
			return false;
		error = L"unrecognized filter type \"" + typeString + L"\"";
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
	bool frequencyGiven = false;
	bool malformed = false;

	found = regex_search(parameters, match, regexFreq);
	if (found)
	{
		wstring freqString = match.str(1);
		freq = getFreq(freqString);
		frequencyGiven = true;
		stream << " with frequency " << freq << " Hz";
	}
	else
	{
		noteError(L"no frequency given");
		malformed = true;
	}

	found = regex_search(parameters, match, regexGain);
	if (found)
	{
		if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::NOTCH || type == BiQuad::ALL_PASS)
			TraceFStatic(L"Ignoring gain for filter of type %s", typeDescription);
		else
		{
			wstring gainString = match.str(1);
			gain = numeric_text::parseDouble(gainString);
			if (type == BiQuad::PEAKING)
				stream << ", gain " << gain << " dB";
			else
				stream << " and gain " << gain << " dB";
		}
	}
	else if (type == BiQuad::PEAKING || type == BiQuad::LOW_SHELF || type == BiQuad::HIGH_SHELF)
	{
		noteError(L"no gain given");
		malformed = true;
	}

	found = regex_search(parameters, match, regexQ);
	if (found)
	{
		wstring qString = match.str(1);
		bandwidthOrQOrS = numeric_text::parseDouble(qString);
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
			bandwidthOrQOrS = numeric_text::parseDouble(bwString);
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
			bandwidthOrQOrS = numeric_text::parseDouble(slopeString);
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
				noteError(L"the order must be 1 or 2");
				malformed = true;
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
		noteError(L"filter parameters must be finite");
		malformed = true;
	}
	else
	{
		// Audit #348 TD-13: a zero or negative frequency, Q, bandwidth or
		// slope makes the coefficient formulas divide by zero or place the
		// poles outside the unit circle. A width of exactly 0 still means
		// "not given" below, as it always has.
		if (frequencyGiven && freq <= 0)
		{
			noteError(L"the frequency must be above 0 Hz");
			malformed = true;
		}
		if (bandwidthOrQOrS < 0)
		{
			noteError(L"Q, bandwidth and slope must be positive");
			malformed = true;
		}
	}

	if (bandwidthOrQOrS == 0)
	{
		if (type == BiQuad::PEAKING || type == BiQuad::ALL_PASS)
		{
			noteError(L"no Q or bandwidth given");
			malformed = true;
		}
		else if (type == BiQuad::LOW_PASS || type == BiQuad::HIGH_PASS || type == BiQuad::BAND_PASS)
		{
			bandwidthOrQOrS = std::numbers::sqrt2_v<double> / 2.0;
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

	if (malformed)
	{
		error = reason;
		return false;
	}

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

bool BiQuadCommand::parse(const wstring& command, wstring& parameters,
	BiQuadCommand& out)
{
	wstring ignoredError;
	return parse(command, parameters, out, ignoredError);
}
