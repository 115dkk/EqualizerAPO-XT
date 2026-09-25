/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#include "stdafx.h"

#include <cmath>
#include <cstdlib>
#include <cwctype>

#include "parser/NumericText.h"

std::wstring numeric_text::normalizeDecimalComma(const std::wstring& value)
{
	std::wstring result = value;
	for (wchar_t& character : result)
	{
		if (character == L',')
			character = L'.';
	}
	return result;
}

namespace
{
	_locale_t cLocale()
	{
		static const _locale_t locale = _create_locale(LC_NUMERIC, "C");
		return locale;
	}
}

double numeric_text::parseDouble(const std::wstring& value)
{
	return _wcstod_l(value.c_str(), nullptr, cLocale());
}

numeric_text::Number numeric_text::readNumber(const std::wstring& text)
{
	// Commas become points one for one, so a length measured in the
	// normalized text is a length in the original.
	const std::wstring normalized = normalizeDecimalComma(text);
	const wchar_t* begin = normalized.c_str();
	wchar_t* end = nullptr;
	const double value = _wcstod_l(begin, &end, cLocale());
	const size_t length = static_cast<size_t>(end - begin);
	if (length == 0 || !std::isfinite(value))
		return Number();

	// wcstod also takes "0x1p3", "inf" and "nan"; a config number is decimal,
	// and x, i and n appear in no decimal spelling.
	for (size_t i = 0; i < length; i++)
	{
		const wchar_t c = static_cast<wchar_t>(std::towlower(begin[i]));
		if (c == L'x' || c == L'i' || c == L'n')
			return Number();
	}
	return Number{value, length};
}

std::optional<double> numeric_text::parseNumber(const std::wstring& text)
{
	size_t first = 0;
	while (first < text.size() && std::iswspace(text[first]))
		first++;
	size_t last = text.size();
	while (last > first && std::iswspace(text[last - 1]))
		last--;
	if (first == last)
		return std::nullopt;

	const std::wstring trimmed = text.substr(first, last - first);
	const Number number = readNumber(trimmed);
	if (number.length != trimmed.size())
		return std::nullopt;
	return number.value;
}
