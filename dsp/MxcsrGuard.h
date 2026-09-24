/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

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
#else
#include <intrin.h>
#endif

// RAII guard that enables Flush-To-Zero (FTZ) and Denormals-Are-Zero (DAZ) on
// MXCSR for the lifetime of the scope, restoring the original value on exit.
// Bit pattern 0x8040 = FTZ (bit 15) | DAZ (bit 6).
//
// ARM64 has no separate DAZ bit: FPCR.FZ (bit 24) flushes subnormal inputs
// and results of single- and double-precision arithmetic, scalar and NEON
// alike, which is what the x64 pair does. The ARM64 build used to leave FPCR
// alone, so a feedback path decaying into the subnormal range produced
// different (and slower) output than on x64 (audit #348, the maintainer's
// decision to match x64).
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
	static constexpr unsigned __int64 kFpcrFlushToZero = 1ull << 24;
	unsigned __int64 saved_;
public:
	MxcsrFtzDazGuard() : saved_(static_cast<unsigned __int64>(_ReadStatusReg(ARM64_FPCR)))
	{
		_WriteStatusReg(ARM64_FPCR, static_cast<__int64>(saved_ | kFpcrFlushToZero));
	}
	~MxcsrFtzDazGuard()
	{
		_WriteStatusReg(ARM64_FPCR, static_cast<__int64>(saved_));
	}
#endif

	MxcsrFtzDazGuard(const MxcsrFtzDazGuard&) = delete;
	MxcsrFtzDazGuard& operator=(const MxcsrFtzDazGuard&) = delete;
};
