/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

namespace MatrixMetrics
{
inline constexpr int gridPitch = 24;
inline constexpr int coordinateBandWidth = 120;
inline constexpr int railInset = 4;
inline constexpr int knobCellHeight = 16;
// The header power key's legend width. With the action cell's 4px side
// padding (matrix_*.qss) the key spans two grid pitches.
inline constexpr int powerKeyWidth = 2 * gridPitch - 8;
}
