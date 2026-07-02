/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#if !defined(_M_ARM64)
#include <immintrin.h>
#endif

// RAII guard that enables Flush-To-Zero (FTZ) and Denormals-Are-Zero (DAZ) on
// MXCSR for the lifetime of the scope, restoring the original value on exit.
// Bit pattern 0x8040 = FTZ (bit 15) | DAZ (bit 6).
//
// Previously the bit pair was toggled inside every BiQuadFilter::process call,
// which meant a configuration with N PEQs paid the load/store twice per block
// per filter. Pulling the guard up to the engine boundary collapses that to
// one pair per process invocation regardless of filter count.
class MxcsrFtzDazGuard
{
#if !defined(_M_ARM64)
	unsigned saved_;
public:
	MxcsrFtzDazGuard() : saved_(_mm_getcsr())
	{
		_mm_setcsr(saved_ | 0x8040u);
	}
	~MxcsrFtzDazGuard()
	{
		_mm_setcsr(saved_);
	}
#else
public:
	MxcsrFtzDazGuard() = default;
	~MxcsrFtzDazGuard() = default;
#endif

	MxcsrFtzDazGuard(const MxcsrFtzDazGuard&) = delete;
	MxcsrFtzDazGuard& operator=(const MxcsrFtzDazGuard&) = delete;
};
