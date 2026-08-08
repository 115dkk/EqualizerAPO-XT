/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2013  Jonas Thedering

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
#include <string>
#include <vector>
#include <cwctype>
#include <cstdlib>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "StringHelper.h"
#include "Win32Resource.h"

using std::find;
using std::string;
using std::vector;
using std::wstring;

wstring StringHelper::replaceCharacters(const wstring& s, const wstring& chars, const wstring& replacement)
{
	wstring result;
	result.reserve(s.length());

	for (unsigned i = 0; i < s.length(); i++)
	{
		wchar_t c = s[i];
		if (chars.find(c) == wstring::npos)
			result += c;
		else
			result += replacement;
	}

	return result;
}

wstring StringHelper::replaceIllegalCharacters(const wstring& filename)
{
	return replaceCharacters(filename, L"<>:\"/\\|?*", L"_");
}

wstring StringHelper::normalizeDecimalComma(const wstring& s)
{
	return replaceCharacters(s, L",", L".");
}

wstring StringHelper::toWString(const string& s, unsigned codepage)
{
	int length = MultiByteToWideChar(codepage, 0, s.c_str(), -1, nullptr, 0);
	if (length == 0)
		return L"";

	vector<wchar_t> charBuf(length);
	MultiByteToWideChar(codepage, 0, s.c_str(), -1, charBuf.data(), length);
	wstring result = charBuf.data();

	return result;
}

string StringHelper::toString(const wstring& s, unsigned codepage)
{
	int length = WideCharToMultiByte(codepage, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (length == 0)
		return "";

	vector<char> charBuf(length);
	WideCharToMultiByte(codepage, 0, s.c_str(), -1, charBuf.data(), length, nullptr, nullptr);
	string result = charBuf.data();

	return result;
}

wstring StringHelper::toLowerCase(const wstring& s)
{
	// Standard per-character case folding (current C locale), replacing the
	// MSVC-only _wcslwr_s. Equivalent for the ASCII text this is used on
	// (GUID strings, channel names) and free of any extra link dependency.
	wstring result = s;
	for (wchar_t& c : result)
		c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));

	return result;
}

wstring StringHelper::toUpperCase(const wstring& s)
{
	wstring result = s;
	for (wchar_t& c : result)
		c = static_cast<wchar_t>(towupper(static_cast<wint_t>(c)));

	return result;
}

wstring StringHelper::trim(const wstring& s)
{
	int firstNonSpace = -1;
	int lastNonSpace = -1;

	for (unsigned i = 0; i < s.length(); i++)
	{
		wchar_t c = s[i];
		if (!iswspace(c))
		{
			if (firstNonSpace == -1)
				firstNonSpace = i;
			lastNonSpace = i;
		}
	}

	if (firstNonSpace == -1)
		return L"";
	else
		return s.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);
}

vector<wstring> StringHelper::split(const wstring& s, wchar_t splitChar, bool skipEmpty)
{
	vector<wstring> result;
	size_t prevPos = 0;
	size_t pos = 0;
	while ((pos = s.find(splitChar, pos)) != wstring::npos)
	{
		wstring part = s.substr(prevPos, pos - prevPos);
		if (part.length() > 0 || !skipEmpty)
			result.push_back(part);
		prevPos = ++pos;
	}

	wstring part = s.substr(prevPos);
	if (part.length() > 0 || !skipEmpty)
		result.push_back(part);

	return result;
}

wstring StringHelper::join(const vector<wstring>& strings, const wstring& separator)
{
	if (strings.empty())
		return L"";

	size_t length = separator.length() * (strings.size() - 1);
	for (const wstring& string : strings)
		length += string.length();

	wstring result;
	result.reserve(length);
	for (vector<wstring>::const_iterator it = strings.cbegin(); it != strings.cend(); it++)
	{
		if (it != strings.cbegin())
			result += separator;
		result += *it;
	}

	return result;
}

wstring StringHelper::getSystemErrorString(long status)
{
	winutil::UniqueLocalPtr<wchar_t> buffer;

	if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, status, 0, reinterpret_cast<LPWSTR>(buffer.put()), 0, nullptr) != 0 && buffer)
	{
		wstring result(buffer.get());

		// remove trailing newline
		if (!result.empty() && result.back() == L'\n')
			result.erase(prev(result.end()));
		if (!result.empty() && result.back() == L'\r')
			result.erase(prev(result.end()));
		return result;
	}
	else
		return L"";
}

vector<wstring> StringHelper::splitQuoted(const wstring& s, wchar_t splitChar, wchar_t quoteChar)
{
	vector<wstring> result;
	bool inQuotes = false;
	wstring current;
	for (size_t i = 0; i < s.length(); i++)
	{
		wchar_t c = s[i];
		if (c == splitChar && !inQuotes)
		{
			if (current != L"")
			{
				result.push_back(current);
				current = L"";
			}
		}
		else if (c == quoteChar)
		{
			inQuotes = !inQuotes;
			if (inQuotes && i > 0 && s[i - 1] == quoteChar)
				current += quoteChar;
		}
		else
		{
			current += c;
		}
	}

	if (current != L"")
		result.push_back(current);

	return result;
}

double StringHelper::parseDouble(const wstring& s)
{
	// Audit #250 F016: the declaration promises locale independence, but a
	// bare wcstod reads LC_NUMERIC. Pin the C locale so the promise holds
	// even inside a host process that set a comma-decimal locale.
	static const _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
	return _wcstod_l(s.c_str(), nullptr, cLocale);
}
