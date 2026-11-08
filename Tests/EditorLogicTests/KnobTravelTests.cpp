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

#include "Editor/widgets/KnobTravel.h"

#include <cmath>

#include "EditorLogicTestSupport.h"

TEST_CASE(EditorLogicTests, testKnobTravelLaw)
{
	// The preamp knob: +-20 dB in 0.1 dB steps is 400 units, so RangePixels
	// of travel is the whole range and one pixel is two units.
	harness.expectNear(KnobTravel::advance(100.0, 1.0, 0, 400, false), 102.0, 1e-9,
		"one pixel up is two units on a 400-unit knob");
	harness.expectNear(KnobTravel::advance(100.0, -1.0, 0, 400, false), 98.0, 1e-9,
		"one pixel down is two units the other way");
	harness.expectNear(KnobTravel::advance(0.0, KnobTravel::RangePixels, 0, 400, false), 400.0, 1e-9,
		"RangePixels of travel sweeps the whole range");

	// A press moves nothing: the drum is grabbed where it is.
	harness.expectNear(KnobTravel::advance(137.0, 0.0, 0, 400, false), 137.0, 1e-9,
		"no travel, no change");

	// Shift: a tenth of the rate.
	harness.expectNear(KnobTravel::advance(100.0, 5.0, 0, 400, true), 101.0, 1e-9,
		"with Shift five pixels are one unit");

	// Slow drags accumulate below a unit instead of being rounded away: on a
	// 100-unit knob a pixel is half a unit, so three pixels are one and a half.
	double carried = 50.0;
	for (int pixel = 0; pixel < 3; pixel++)
		carried = KnobTravel::advance(carried, 1.0, 0, 100, false);
	harness.expectNear(carried, 51.5, 1e-9, "fractional travel is carried across moves");

	// The ends: an overshoot stops at the end, and the first pixel back moves
	// the value again (a rubber band would have to unwind the overshoot first).
	const double atTop = KnobTravel::advance(398.0, 10.0, 0, 400, false);
	harness.expectNear(atTop, 400.0, 1e-9, "travel past the top stops at the top");
	harness.expectNear(KnobTravel::advance(atTop, -1.0, 0, 400, false), 398.0, 1e-9,
		"the first pixel back from the top moves the value");
	const double atBottom = KnobTravel::advance(-199.0, -30.0, -200, 200, false);
	harness.expectNear(atBottom, -200.0, 1e-9, "travel past the bottom stops at the bottom (range below zero)");
	harness.expectNear(KnobTravel::advance(atBottom, 1.0, -200, 200, false), -198.0, 1e-9,
		"the first pixel back from the bottom moves the value");

	// An empty range is inert rather than a division by zero elsewhere.
	harness.expectNear(KnobTravel::advance(7.0, 50.0, 7, 7, false), 7.0, 1e-9, "an empty range stays put");
}

// The rotary law behind AudioKnob's Rotary gesture (every arc knob). #456: a
// press on the knob's right edge used to jump the value to that angle, and
// on the small card knobs a few pixels of hand movement swept most of the
// range. The knob is still turned, not dragged up and down (the maintainer
// rejected vertical drag for arc knobs), but only by the angle swept after
// the press, and never faster than a knob of MinimumRadiusPixels.
TEST_CASE(EditorLogicTests, testKnobTurnLaw)
{
	// Angles: positions relative to the centre, y down, clockwise positive.
	// Measured well outside the minimum radius, a quarter turn is 90 degrees.
	const double outside = KnobTravel::MinimumRadiusPixels * 3.0;
	harness.expectNear(KnobTravel::sweptDegrees(0.0, -outside, outside, 0.0), 90.0, 1e-9,
		"top to right is a quarter turn clockwise");
	harness.expectNear(KnobTravel::sweptDegrees(outside, 0.0, 0.0, -outside), -90.0, 1e-9,
		"right to top is a quarter turn back");
	harness.expectNear(KnobTravel::sweptDegrees(0.0, -outside, 0.0, -2.0 * outside), 0.0, 1e-9,
		"moving straight out from the centre turns nothing");
	harness.expectNear(KnobTravel::sweptDegrees(0.0, -outside, 0.0, -outside), 0.0, 1e-9,
		"a press that does not move turns nothing");
	harness.expectNear(KnobTravel::sweptDegrees(0.0, 0.0, outside, 0.0), 0.0, 1e-9,
		"the exact centre has no angle and turns nothing");

	// Near the centre the arc length counts, at the minimum radius: a tiny
	// circle no longer sweeps the knob end to end.
	const double inside = KnobTravel::MinimumRadiusPixels / 4.0;
	harness.expectNear(KnobTravel::sweptDegrees(0.0, -inside, inside, 0.0), 90.0 / 4.0, 1e-9,
		"a quarter circle at a quarter of the minimum radius turns a quarter as far");
	const double arcPixels = 10.0;
	const double stepDegrees = arcPixels / inside * 180.0 / 3.14159265358979323846;
	harness.expectNear(KnobTravel::sweptDegrees(0.0, -inside, std::sin(stepDegrees * 3.14159265358979323846 / 180.0) * inside,
			-std::cos(stepDegrees * 3.14159265358979323846 / 180.0) * inside),
		arcPixels / KnobTravel::MinimumRadiusPixels * 180.0 / 3.14159265358979323846, 1e-6,
		"inside the minimum radius ten pixels of arc turn as far as they would at that radius");
	harness.expectNear(KnobTravel::MinimumRadiusPixels * KnobTravel::SweepDegrees * 3.14159265358979323846 / 180.0,
		static_cast<double>(KnobTravel::RangePixels), 1e-9,
		"a full sweep at the minimum radius is the vertical drag's hand travel");

	// Values: the painted 270-degree arc is the whole range, Shift a tenth.
	harness.expectNear(KnobTravel::turn(0.0, KnobTravel::SweepDegrees, 0, 400, false), 400.0, 1e-9,
		"the full arc sweeps the whole range");
	harness.expectNear(KnobTravel::turn(200.0, 27.0, 0, 400, false), 240.0, 1e-9,
		"27 degrees is a tenth of the range");
	harness.expectNear(KnobTravel::turn(200.0, 27.0, 0, 400, true), 204.0, 1e-9,
		"with Shift 27 degrees is a hundredth");
	harness.expectNear(KnobTravel::turn(137.0, 0.0, 0, 400, false), 137.0, 1e-9,
		"no turn, no change: a click keeps the value");
	const double pegged = KnobTravel::turn(390.0, 90.0, 0, 400, false);
	harness.expectNear(pegged, 400.0, 1e-9, "turning past the end stops at the end");
	harness.expectNear(KnobTravel::turn(pegged, -2.7, 0, 400, false), 396.0, 1e-9,
		"the first degree back from the end moves the value");
}
