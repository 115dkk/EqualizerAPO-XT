/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <cstddef>

#include "hwy/highway.h"

// The float <-> double sample conversion kernel, spelled once (audit #275
// A1/TD-22). It used to exist three times: as file-local helpers in
// FilterConfiguration.cpp, as global functions in FilterEngine.Process.cpp,
// and - reached through hand-written extern prototypes with no header, so a
// rename compiled and broke at link time - in VSTPluginFilter.cpp.
// SampleIoTests pins the promotion/demotion bit-level against the scalar
// loops; with one kernel that pin covers every consumer.
//
// Header-inline on purpose: each consumer TU compiles the kernel with its own
// variant's /arch, exactly as the three copies did.
namespace sampleconv
{
namespace hn = hwy::HWY_NAMESPACE;

// Promote float -> double (exact). One portable Highway loop; NEON on ARM64.
inline void promote(double* dest, const float* src, size_t count)
{
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;  // float tag with dd's lane count
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= count; i += N)
		hn::StoreU(hn::PromoteTo(dd, hn::LoadU(df, src + i)), dd, dest + i);
	for (; i < count; i++)
		dest[i] = static_cast<double>(src[i]);
}

// Demote double -> float (round to nearest even, same as static_cast<float>).
inline void demote(float* dest, const double* src, size_t count)
{
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= count; i += N)
		hn::StoreU(hn::DemoteTo(df, hn::LoadU(dd, src + i)), df, dest + i);
	for (; i < count; i++)
		dest[i] = static_cast<float>(src[i]);
}
}
