/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>
#include <string>

namespace vsttext
{
// A plug-in can fill an ABI character array without a terminator.
// testForeignParameterNames and testForeignClassName reproduce it with
// TestVst2Plugin/TestVst3Plugin (audit #467 TD-52).
template<size_t Size>
std::string boundedString(const char (&value)[Size])
{
	return std::string(value, std::find(value, value + Size, '\0'));
}
}
