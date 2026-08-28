/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#define _USE_MATH_DEFINES

#include "BiQuadWidthConversion.h"

#include <cmath>

namespace BiQuadWidth
{
double bandwidthFromQ(double q)
{
	if (!(q > 0.0) || !std::isfinite(q))
		return 0.0;
	return 2.0 / M_LN2 * std::asinh(1.0 / (2.0 * q));
}

double qFromBandwidth(double octaves)
{
	if (!std::isfinite(octaves) || octaves <= 0.0)
		return 0.0;
	const double p2n = std::pow(2.0, octaves);
	if (p2n == 1.0)
		return 0.0;
	return std::sqrt(p2n) / (p2n - 1.0);
}

bool offersBandwidth(BiQuad::Type type)
{
	return type == BiQuad::PEAKING || type == BiQuad::ALL_PASS;
}

double defaultQ(BiQuad::Type type)
{
	switch (type)
	{
	case BiQuad::ALL_PASS:
		return M_SQRT1_2;
	case BiQuad::PEAKING:
		return 10.0;
	case BiQuad::LOW_PASS:
	case BiQuad::HIGH_PASS:
	case BiQuad::BAND_PASS:
		return M_SQRT1_2;
	case BiQuad::NOTCH:
		return 30.0;
	case BiQuad::LOW_SHELF:
	case BiQuad::HIGH_SHELF:
		return 0.9;
	}
	return M_SQRT1_2;
}
}
