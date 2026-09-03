/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The vertical-drag law behind AudioKnob's VerticalDrag gesture (the
	minimal skin's register drum). The law is what makes the drum feel like
	a drum: a press grabs it without moving it, travel up increases, Shift
	slows it tenfold, and the ends stop it without a rubber band. Any of
	these regressing is felt in the hand before it is seen, so they are
	pinned here.
*/

#include <cmath>

#include "Editor/widgets/KnobTravel.h"

#include "EditorLogicTestSupport.h"

namespace
{
bool sameValue(double actual, double expected)
{
	return std::fabs(actual - expected) < 1e-9;
}
}

void testKnobTravelLaw()
{
	// The preamp knob: +-20 dB in 0.1 dB steps is 400 units, so RangePixels
	// of travel is the whole range and one pixel is two units.
	expectTrue(sameValue(KnobTravel::advance(100.0, 1.0, 0, 400, false), 102.0),
		"one pixel up is two units on a 400-unit knob");
	expectTrue(sameValue(KnobTravel::advance(100.0, -1.0, 0, 400, false), 98.0),
		"one pixel down is two units the other way");
	expectTrue(sameValue(KnobTravel::advance(0.0, KnobTravel::RangePixels, 0, 400, false), 400.0),
		"RangePixels of travel sweeps the whole range");

	// A press moves nothing: the drum is grabbed where it is.
	expectTrue(sameValue(KnobTravel::advance(137.0, 0.0, 0, 400, false), 137.0),
		"no travel, no change");

	// Shift: a tenth of the rate.
	expectTrue(sameValue(KnobTravel::advance(100.0, 5.0, 0, 400, true), 101.0),
		"with Shift five pixels are one unit");

	// Slow drags accumulate below a unit instead of being rounded away: on a
	// 100-unit knob a pixel is half a unit, so three pixels are one and a half.
	double carried = 50.0;
	for (int pixel = 0; pixel < 3; pixel++)
		carried = KnobTravel::advance(carried, 1.0, 0, 100, false);
	expectTrue(sameValue(carried, 51.5), "fractional travel is carried across moves");

	// The ends: an overshoot stops at the end, and the first pixel back moves
	// the value again (a rubber band would have to unwind the overshoot first).
	const double atTop = KnobTravel::advance(398.0, 10.0, 0, 400, false);
	expectTrue(sameValue(atTop, 400.0), "travel past the top stops at the top");
	expectTrue(sameValue(KnobTravel::advance(atTop, -1.0, 0, 400, false), 398.0),
		"the first pixel back from the top moves the value");
	const double atBottom = KnobTravel::advance(-199.0, -30.0, -200, 200, false);
	expectTrue(sameValue(atBottom, -200.0), "travel past the bottom stops at the bottom (range below zero)");
	expectTrue(sameValue(KnobTravel::advance(atBottom, 1.0, -200, 200, false), -198.0),
		"the first pixel back from the bottom moves the value");

	// An empty range is inert rather than a division by zero elsewhere.
	expectTrue(sameValue(KnobTravel::advance(7.0, 50.0, 7, 7, false), 7.0), "an empty range stays put");
}
