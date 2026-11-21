/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <limits>

inline unsigned addLatency(unsigned a, unsigned b) noexcept
{
	const unsigned maximum = (std::numeric_limits<unsigned>::max)();
	return b > maximum - a ? maximum : a + b;
}
