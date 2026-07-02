/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch

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

#include "LoudnessCorrectionCommand.h"

#include <cstdio>
#include <cstdlib>
#include <regex>

using std::wregex;
using std::wsmatch;
using std::wstring;

// Regexes preserved from FilterParameters::deSerialize. ReferenceLevel and
// ReferenceOffset only accept integers; Attenuation only accepts values
// between 0 and 1 (a decimal comma is tolerated by the regex but wcstod stops
// at it, like the previous conversion did).
static wregex regexState(L"\\s*State\\s+(0|1)");
static wregex regexReferenceLevel(L"\\s*ReferenceLevel\\s+([-+0-9]+)");
static wregex regexReferenceOffset(L"\\s*ReferenceOffset\\s+([-+0-9]+)");
static wregex regexAttenuation(L"\\s*Attenuation\\s+((1((\\.|,)0+)?)|(0((\\.|,)[0-9]+)?))");

wstring LoudnessCorrectionCommand::serialize() const
{
	wstring result = L"State " + std::to_wstring(state ? 1 : 0)
		+ L" ReferenceLevel " + std::to_wstring(static_cast<int>(referenceLevel))
		+ L" ReferenceOffset " + std::to_wstring(static_cast<int>(referenceOffset))
		+ L" Attenuation ";

	wchar_t buffer[32];
	if (attenuation == 0.0f || attenuation == 1.0f)
		swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.1f", attenuation);
	else
		swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%g", attenuation);
	result += buffer;

	return result;
}

bool LoudnessCorrectionCommand::parse(const wstring& command, const wstring& parameters, LoudnessCorrectionCommand& out)
{
	if (command != L"LoudnessCorrection")
		return false;

	wsmatch match;
	if (!regex_search(parameters, match, regexState))
		return false;
	out.state = wcstol(match.str(1).c_str(), nullptr, 10) != 0;

	if (!regex_search(parameters, match, regexReferenceLevel))
		return false;
	out.referenceLevel = static_cast<float>(wcstod(match.str(1).c_str(), nullptr));

	if (!regex_search(parameters, match, regexReferenceOffset))
		return false;
	out.referenceOffset = static_cast<float>(wcstod(match.str(1).c_str(), nullptr));

	if (regex_search(parameters, match, regexAttenuation))
		out.attenuation = static_cast<float>(wcstod(match.str(1).c_str(), nullptr));
	else
		out.attenuation = 1.0f;

	return true;
}
