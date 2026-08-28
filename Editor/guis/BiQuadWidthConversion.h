/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	How wide a biquad's effect is, and the two ways the config file spells it.
*/

#pragma once

#include "filters/BiQuad.h"

// A biquad's width is written either as a Q factor or as a bandwidth in
// octaves. Both editors - the legacy per-filter GUI and the modern all-pass
// card - have to offer the same choice and convert between the two the same
// way, or the same configuration line means different things depending on which
// editor opened it. That is not hypothetical: an all-pass written as
// "BW Oct 1" used to come back as "Q 1" because the legacy editor offered the
// all-pass no bandwidth entry to restore, which shortens the group delay at Fc
// by a factor of sqrt(2).
//
// Qt-free on purpose, so the tests can hold both editors to it without a
// widget.
namespace BiQuadWidth
{
// Q -> bandwidth in octaves.
double bandwidthFromQ(double q);

// Bandwidth in octaves -> Q.
//
// Exactly the inverse of bandwidthFromQ, because 1/(2q) = sinh(ln2 * n / 2)
// solves both ways; a round trip returns the number it started from.
double qFromBandwidth(double octaves);

// Whether the editors let this filter type's width be written as a bandwidth
// as well as a Q.
//
// The engine and the parser accept "BW Oct" for every type whose width is a Q,
// because BiQuad's alpha branch for bandwidth does not look at the type at all.
// The editors are narrower than that, and deliberately so for now: opening the
// choice on a type nobody has reported a round-trip problem for would change
// what those editors write without anyone asking. Peaking has always had it;
// all-pass is added because writing it back as a Q was audibly wrong.
bool offersBandwidth(BiQuad::Type type);

// The width a newly created filter of this type starts at, as a Q.
//
// 0.707 for an all-pass rather than the 10 the editors used to create: an
// all-pass is a phase tool, and Q 10 concentrates two thirds of a second of
// group delay into a narrow band, which is a strange place to start from and
// not what anyone reaches for first. Existing configurations are never
// migrated - this is the value for new filters only.
double defaultQ(BiQuad::Type type);
}
