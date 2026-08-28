/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2013  Jonas Thedering
*/

#include "stdafx.h"

#include <cwctype>

#include "text/WideString.h"

std::wstring text::replaceCharacters(const std::wstring& value, const std::wstring& characters,
	const std::wstring& replacement)
{
	std::wstring result;
	result.reserve(value.length());
	for (wchar_t character : value)
		result += characters.find(character) == std::wstring::npos ? std::wstring(1, character) : replacement;
	return result;
}

std::wstring text::replaceIllegalFilenameCharacters(const std::wstring& filename)
{
	return replaceCharacters(filename, L"<>:\"/\\|?*", L"_");
}

std::wstring text::toLower(const std::wstring& value)
{
	std::wstring result = value;
	for (wchar_t& character : result)
		character = static_cast<wchar_t>(towlower(static_cast<wint_t>(character)));
	return result;
}

std::wstring text::toUpper(const std::wstring& value)
{
	std::wstring result = value;
	for (wchar_t& character : result)
		character = static_cast<wchar_t>(towupper(static_cast<wint_t>(character)));
	return result;
}

std::wstring text::trim(const std::wstring& value)
{
	size_t first = 0;
	while (first < value.size() && iswspace(value[first]))
		++first;
	if (first == value.size())
		return L"";
	size_t last = value.size();
	while (last > first && iswspace(value[last - 1]))
		--last;
	return value.substr(first, last - first);
}

std::vector<std::wstring> text::split(const std::wstring& value, wchar_t delimiter, bool skipEmpty)
{
	std::vector<std::wstring> result;
	size_t previous = 0;
	size_t position = 0;
	while ((position = value.find(delimiter, position)) != std::wstring::npos)
	{
		std::wstring part = value.substr(previous, position - previous);
		if (!part.empty() || !skipEmpty)
			result.push_back(std::move(part));
		previous = ++position;
	}
	std::wstring part = value.substr(previous);
	if (!part.empty() || !skipEmpty)
		result.push_back(std::move(part));
	return result;
}

std::wstring text::join(const std::vector<std::wstring>& values, const std::wstring& separator)
{
	if (values.empty())
		return L"";
	size_t length = separator.length() * (values.size() - 1);
	for (const std::wstring& value : values)
		length += value.length();
	std::wstring result;
	result.reserve(length);
	bool first = true;
	for (const std::wstring& value : values)
	{
		if (!first)
			result += separator;
		first = false;
		result += value;
	}
	return result;
}

std::vector<std::wstring> text::splitQuoted(const std::wstring& value, wchar_t delimiter, wchar_t quote)
{
	std::vector<std::wstring> result;
	bool inQuotes = false;
	std::wstring current;
	for (size_t i = 0; i < value.length(); ++i)
	{
		const wchar_t character = value[i];
		if (character == delimiter && !inQuotes)
		{
			if (!current.empty())
			{
				result.push_back(current);
				current.clear();
			}
		}
		else if (character == quote)
		{
			inQuotes = !inQuotes;
			if (inQuotes && i > 0 && value[i - 1] == quote)
				current += quote;
		}
		else
		{
			current += character;
		}
	}
	if (!current.empty())
		result.push_back(current);
	return result;
}
