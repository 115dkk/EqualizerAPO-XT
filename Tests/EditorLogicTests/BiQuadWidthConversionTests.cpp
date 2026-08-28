/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The width conversion both filter editors call.

	It exists as shared code rather than as two copies because the legacy
	per-filter GUI and the all-pass card have to open and save the same
	configuration line identically. When only one of them knew that an all-pass
	can be written as a bandwidth, "BW Oct 1" came back as "Q 1" - a different
	filter, quiet enough to miss and large enough to hear.
*/

#define _USE_MATH_DEFINES

#include <cmath>
#include <limits>

#include "Editor/guis/BiQuadWidthConversion.h"

#include "EditorLogicTestSupport.h"

void testBiQuadWidthRoundTripsExactly()
{
	// The pairs the reform document names. Both directions, then back again:
	// 1/(2q) = sinh(ln2 * n / 2) solves either way, so a round trip has to
	// return the number it started from rather than something close to it.
	// Values to seven decimals, computed from 2/ln2 * asinh(1/(2q)). The reform
	// document's table gives the first three rounded to five, which is where
	// its 1.90000 and 0.14421 come from; the rounding is checked separately
	// below so the document and the code cannot drift apart unnoticed.
	struct Pair { double q; double octaves; };
	const Pair pairs[] = {
		{0.7071068, 1.8999686},
		{1.4142136, 1.0000000},
		{10.0, 0.1442095},
		{0.3333, 3.4475980},
		{33.3333, 0.0432793},
	};

	for (const Pair& pair : pairs)
	{
		const double octaves = BiQuadWidth::bandwidthFromQ(pair.q);
		expectTrue(std::abs(octaves - pair.octaves) < 5e-7,
			QStringLiteral("Q %1 is %2 octaves (got %3)")
				.arg(pair.q, 0, 'g', 8).arg(pair.octaves, 0, 'f', 7).arg(octaves, 0, 'f', 7));

		const double q = BiQuadWidth::qFromBandwidth(octaves);
		expectTrue(std::abs(q - pair.q) < pair.q * 1e-12,
			QStringLiteral("Q %1 -> octaves -> Q returns the same number (got %2)")
				.arg(pair.q, 0, 'g', 8).arg(q, 0, 'g', 8));

		const double back = BiQuadWidth::bandwidthFromQ(BiQuadWidth::qFromBandwidth(pair.octaves));
		expectTrue(std::abs(back - pair.octaves) < pair.octaves * 1e-12,
			QStringLiteral("%1 octaves -> Q -> octaves returns the same number (got %2)")
				.arg(pair.octaves, 0, 'f', 7).arg(back, 0, 'f', 7));
	}

	// The reform document (section 12.2) states three pairs to five decimals.
	// Two of them are exact to that precision; the third is not, and the value
	// asserted here is the correct one.
	//
	// The document pairs Q 0.7071068 with 1.90000 octaves, but 1/sqrt(2)
	// converts to 1.89997. The exact partner of 1.90000 octaves is Q 0.707093,
	// a different number that happens to round to the same four decimals. The
	// pairing was an approximation, not a definition, and the conversion is not
	// free to reproduce it.
	struct Documented { double q; const char* octaves; };
	const Documented documented[] = {
		{1.4142136, "1.00000"},
		{10.0, "0.14421"},
		{0.7071068, "1.89997"},
	};
	for (const Documented& entry : documented)
	{
		expectEqual(QString::number(BiQuadWidth::bandwidthFromQ(entry.q), 'f', 5),
			QString::fromLatin1(entry.octaves),
			QStringLiteral("Q %1 converts to the stated bandwidth").arg(entry.q, 0, 'g', 8));
	}
	// And the other direction of that pair, so the correction is anchored on
	// both sides rather than on one assertion.
	expectEqual(QString::number(BiQuadWidth::qFromBandwidth(1.9), 'f', 6), QStringLiteral("0.707093"),
		QStringLiteral("1.90000 octaves is Q 0.707093, not 1/sqrt(2)"));

	// A wider band is a lower Q, monotonically. Worth stating because the two
	// formulas run in opposite directions and a sign slip in either would still
	// round-trip.
	expectTrue(BiQuadWidth::bandwidthFromQ(0.5) > BiQuadWidth::bandwidthFromQ(5.0),
		QStringLiteral("a low Q is a wide band"));
	expectTrue(BiQuadWidth::qFromBandwidth(0.25) > BiQuadWidth::qFromBandwidth(2.0),
		QStringLiteral("a narrow band is a high Q"));

	// Degenerate inputs answer zero rather than an infinity or a NaN that would
	// then be written into a configuration file.
	for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::quiet_NaN()})
	{
		expectTrue(BiQuadWidth::bandwidthFromQ(bad) == 0.0,
			QStringLiteral("a nonsensical Q converts to zero, not to a nonsensical bandwidth"));
		expectTrue(BiQuadWidth::qFromBandwidth(bad) == 0.0,
			QStringLiteral("a nonsensical bandwidth converts to zero, not to a nonsensical Q"));
	}
}

void testBiQuadWidthModesAndDefaults()
{
	// The two types whose editors offer both spellings. Peaking always had it;
	// the all-pass is the fix.
	expectTrue(BiQuadWidth::offersBandwidth(BiQuad::ALL_PASS),
		QStringLiteral("an all-pass width can be written as a bandwidth"));
	expectTrue(BiQuadWidth::offersBandwidth(BiQuad::PEAKING),
		QStringLiteral("a peaking width can be written as a bandwidth"));
	// Deliberately unchanged: the engine would accept a bandwidth for these
	// too, but opening the choice on a type nobody has reported a round-trip
	// problem for would change what the editors write without anyone asking.
	for (BiQuad::Type type : {BiQuad::LOW_PASS, BiQuad::HIGH_PASS, BiQuad::BAND_PASS,
		BiQuad::NOTCH, BiQuad::LOW_SHELF, BiQuad::HIGH_SHELF})
	{
		expectFalse(BiQuadWidth::offersBandwidth(type),
			QStringLiteral("the editors still offer only a Q for filter type %1").arg(static_cast<int>(type)));
	}

	// A new all-pass starts at 0.707, not at the 10 the editors used to create.
	// Q 10 at 1 kHz concentrates about 30 ms of group delay into a narrow band,
	// which is a strange place to start a phase correction from.
	expectTrue(std::abs(BiQuadWidth::defaultQ(BiQuad::ALL_PASS) - M_SQRT1_2) < 1e-12,
		QStringLiteral("a new all-pass starts at Q 0.707"));
	// Peaking keeps its own default: this reform does not touch it.
	expectTrue(BiQuadWidth::defaultQ(BiQuad::PEAKING) == 10.0,
		QStringLiteral("a new peaking filter still starts at Q 10"));
	expectTrue(BiQuadWidth::defaultQ(BiQuad::NOTCH) == 30.0,
		QStringLiteral("a new notch still starts at Q 30"));
}
