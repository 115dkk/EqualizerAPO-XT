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
#include "text/WideString.h"

#include "DeviceCommand.h"

#include <regex>


using std::vector;
using std::wregex;
using std::wstring;

static wregex regexGuid(L"\\{[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\}");

bool DeviceCommand::matches(const wstring& deviceString) const
{
	wstring deviceStringNoGuid = regex_replace(deviceString, regexGuid, L"");

	// Matching loop preserved from the engine factory: every word of a pattern
	// must occur for the pattern to match; the first matching pattern wins.
	bool matches = false;

	for (unsigned i = 0; i < patterns.size(); i++)
	{
		matches = true;

		if (patterns[i].size() == 1 && text::toLower(patterns[i][0]) == L"all")
			break;

		for (unsigned j = 0; j < patterns[i].size(); j++)
		{
			wstring word = text::toLower(patterns[i][j]);
			const wstring& matchString = word.find('{') == wstring::npos ? deviceStringNoGuid : deviceString;
			if (text::toLower(matchString).find(word) == wstring::npos)
			{
				matches = false;
				break;
			}
		}

		if (matches)
			break;
	}

	return matches;
}

wstring DeviceCommand::serialize() const
{
	wstring result;
	for (const vector<wstring>& pattern : patterns)
	{
		if (!result.empty())
			result += L"; ";

		wstring patternString;
		for (const wstring& word : pattern)
		{
			if (!patternString.empty())
				patternString += L" ";
			patternString += word;
		}
		result += patternString;
	}
	return result;
}

DeviceCommand DeviceCommand::fromPattern(const wstring& pattern)
{
	DeviceCommand result;

	// Tokenizer preserved from the engine factory: split on spaces and
	// semicolons, dropping empty words and empty patterns.
	wstring value = text::trim(pattern) + L";";

	vector<wstring> currentList;
	wstring currentWord;

	for (unsigned i = 0; i < value.length(); i++)
	{
		wchar_t c = value[i];
		if (c == L' ' || c == L';')
		{
			if (currentWord.length() > 0)
			{
				currentList.push_back(currentWord);
				currentWord.clear();
			}
			if (c == L';' && currentList.size() > 0)
			{
				result.patterns.push_back(currentList);
				currentList.clear();
			}
		}
		else
		{
			currentWord += c;
		}
	}

	return result;
}

bool DeviceCommand::parse(const wstring& command, const wstring& parameters, DeviceCommand& out)
{
	if (command != L"Device")
		return false;

	out = fromPattern(parameters);

	return true;
}
