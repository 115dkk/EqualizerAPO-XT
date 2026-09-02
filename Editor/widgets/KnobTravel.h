/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>

// The vertical-drag law of AudioKnob (KnobGesture::VerticalDrag), kept free
// of widgets so EditorLogicTests can pin it and so a skin that paints a
// rolling surface can share the one figure that makes the surface follow
// the pointer one to one.
namespace KnobTravel
{
// Pointer travel, in logical pixels, that sweeps a knob across its whole
// range. 200px is the figure most audio software settles on for a vertical
// knob drag: a wrist-scale movement for the full range, still two units per
// pixel on a 400-step gain knob.
constexpr int RangePixels = 200;

// Shift divides the rate by this much for fine adjustment.
constexpr double FineDivisor = 10.0;

// The value after the pointer travelled travelPixels upward (negative:
// downward) since the last move, carried in fractional units so slow drags
// are not lost to rounding. Clamped at every step: a drag past either end
// stops there, and a reversal moves the value at once instead of first
// unwinding the overshoot (no rubber band).
inline double advance(double value, double travelPixels, int minimum, int maximum, bool fine)
{
	double unitsPerPixel = (maximum - minimum) / static_cast<double>(RangePixels);
	if (fine)
		unitsPerPixel /= FineDivisor;
	return std::clamp(value + travelPixels * unitsPerPixel,
		static_cast<double>(minimum), static_cast<double>(maximum));
}
}
