/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
*/

#pragma once

#include <string>

namespace numeric_text
{
	std::wstring normalizeDecimalComma(const std::wstring& value);
	double parseDouble(const std::wstring& value);
}
