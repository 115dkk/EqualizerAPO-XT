/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

// The drag laws of AudioKnob, kept free of widgets so EditorLogicTests can
// pin them and so a skin that paints a rolling surface can share the one
// figure that makes the surface follow the pointer one to one.
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
// unwinding the overshoot (no rubber band). KnobGesture::VerticalDrag.
inline double advance(double value, double travelPixels, int minimum, int maximum, bool fine)
{
	double unitsPerPixel = (maximum - minimum) / static_cast<double>(RangePixels);
	if (fine)
		unitsPerPixel /= FineDivisor;
	return std::clamp(value + travelPixels * unitsPerPixel,
		static_cast<double>(minimum), static_cast<double>(maximum));
}

// KnobGesture::Rotary. The knob is still turned, not dragged up and down:
// the press grabs it where it stands, and the value follows the angle the
// pointer sweeps around the knob's centre from there, clockwise up, the
// painted 270-degree arc for the whole range. It used to jump to the angle
// under the press, so a click on the right edge of a gain knob was +20 dB at
// once (#456).
constexpr double SweepDegrees = 270.0;

// The radius the sweep is measured at never drops below this. Near the
// centre a small hand movement covers a large angle, which made the small
// card knobs twitchy (#456); inside this radius the knob turns by arc length
// instead, so a turn takes the same hand travel whatever size the knob is
// painted at. At this radius a full sweep is RangePixels of arc, the
// vertical drag's figure.
constexpr double MinimumRadiusPixels = RangePixels / (SweepDegrees * std::numbers::pi / 180.0);

// Degrees the pointer swept around the centre moving from (fromX, fromY) to
// (toX, toY), both relative to the centre in widget coordinates (y grows
// downward), positive clockwise. Radial movement sweeps nothing.
inline double sweptDegrees(double fromX, double fromY, double toX, double toY)
{
	const double fromRadius = std::hypot(fromX, fromY);
	const double toRadius = std::hypot(toX, toY);
	if (fromRadius <= 0.0 || toRadius <= 0.0)
		return 0.0;
	const double degrees = std::atan2(fromX * toY - fromY * toX, fromX * toX + fromY * toY)
		* 180.0 / std::numbers::pi;
	const double radius = std::sqrt(fromRadius * toRadius);
	return radius >= MinimumRadiusPixels ? degrees : degrees * radius / MinimumRadiusPixels;
}

// The value after the knob was turned by degrees (negative:
// counter-clockwise), carried and clamped the way advance() does it.
inline double turn(double value, double degrees, int minimum, int maximum, bool fine)
{
	double unitsPerDegree = (maximum - minimum) / SweepDegrees;
	if (fine)
		unitsPerDegree /= FineDivisor;
	return std::clamp(value + degrees * unitsPerDegree,
		static_cast<double>(minimum), static_cast<double>(maximum));
}
}
