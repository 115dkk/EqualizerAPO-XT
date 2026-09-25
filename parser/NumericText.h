/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace numeric_text
{
	std::wstring normalizeDecimalComma(const std::wstring& value);
	double parseDouble(const std::wstring& value);

	// A number read the way config lines write numbers (audit #348 F2/TD-42):
	// in the C locale whatever the process locale is, with a decimal comma
	// read as a point, in decimal notation only (no hex, no inf, no nan), and
	// finite. The commands used to assemble this from wcstod, swscanf_s and
	// wstringstream each their own way, and one of them read "0,5" as 0.
	struct Number
	{
		double value = 0.0;
		// How many characters of the text the number took; 0 when no number
		// starts there.
		size_t length = 0;
	};

	// The number at the start of `text`, after any leading whitespace.
	Number readNumber(const std::wstring& text);

	// `text`, surrounding whitespace aside, is one number and nothing else.
	std::optional<double> parseNumber(const std::wstring& text);
}
