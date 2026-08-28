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

#include <string>

namespace numeric_text
{
	std::wstring normalizeDecimalComma(const std::wstring& value);
	double parseDouble(const std::wstring& value);
}
