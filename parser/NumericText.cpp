/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
*/

#include "stdafx.h"

#include <cstdlib>

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

double numeric_text::parseDouble(const std::wstring& value)
{
	static const _locale_t cLocale = _create_locale(LC_NUMERIC, "C");
	return _wcstod_l(value.c_str(), nullptr, cLocale);
}
