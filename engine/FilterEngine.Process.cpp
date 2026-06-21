/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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

#include "stdafx.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <exception>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Shlwapi.h>
#include <Ks.h>
#include <KsMedia.h>
#include <mpParser.h>
#include <mpPackageCommon.h>
#include <mpPackageNonCmplx.h>
#include <mpPackageStr.h>
#include <mpPackageMatrix.h>

#include "helpers/RegistryHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/MemoryHelper.h"
#include "helpers/ChannelHelper.h"
#include "helpers/PerfProfile.h"
#include "helpers/MxcsrGuard.h"
#include "ConfigurationFileReader.h"
#include "FilterEngine.h"
// Filter factory headers intentionally omitted: the factories self-register and
// are pulled into the link via /WHOLEARCHIVE in the consumers; this hot-path TU
// names none of them (see FilterEngine.Configuration.cpp).

// stdafx.h pulls in <windows.h> without NOMINMAX, so min/max are defined as
// macros here. Undefine them before Highway, whose templates use std::min and
// std::max.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "hwy/highway.h"

namespace hn = hwy::HWY_NAMESPACE;

using std::exception;
using std::find;
using std::lock_guard;
using std::make_unique;
using std::max;
using std::move;
using std::mutex;
using std::string;
using std::stringstream;
using std::swap;
using std::thread;
using std::unique_lock;
using std::vector;
using std::wstring;
using namespace mup;


#pragma AVRT_CODE_BEGIN
void convertFloatToDouble(double* dest, const float* src, size_t count) {
	// Promote float -> double (exact). One portable Highway loop replaces the
	// AVX-512/AVX2 cvtps_pd ladder and adds the NEON path for ARM64.
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;  // float tag with dd's lane count
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= count; i += N) {
		const auto f = hn::LoadU(df, src + i);
		hn::StoreU(hn::PromoteTo(dd, f), dd, dest + i);
	}
	for (; i < count; ++i) dest[i] = static_cast<double>(src[i]);
}

// Converts a block of doubles back to floats.
void convertDoubleToFloat(float* dest, const double* src, size_t count) {
	// Demote double -> float (round to nearest even, same as the old cvtpd_ps
	// and static_cast<float>). One portable Highway loop, NEON on ARM64.
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= count; i += N) {
		const auto v = hn::LoadU(dd, src + i);
		hn::StoreU(hn::DemoteTo(df, v), df, dest + i);
	}
	for (; i < count; ++i) dest[i] = static_cast<float>(src[i]);
}


// Process interleaved audio (float*)
void FilterEngine::process(float* output, float* input, unsigned frameCount)
{
	PerfScope _eapo_total("FilterEngine::process(float interleaved)");
	MxcsrFtzDazGuard _mxcsrGuard;

	if (currentConfig->isEmpty() && !nextConfigReady.load(std::memory_order_acquire))
	{
		// Bypass mode: if no filters are active, just copy input to output if necessary.
		if (realChannelCount == outputChannelCount && input != output) {
			std::copy_n(input, outputChannelCount * frameCount, output);
		}
		return;
	}

	// Fused float32 -> double + deinterleave directly into planar storage,
	// avoiding the extra inputBuf1D pass that the original two-step path used.
	{
		PerfScope _ps("FilterConfiguration::readFloatInterleaved(current)");
		currentConfig->readFloatInterleaved(input, frameCount);
	}
	{
		PerfScope _ps("FilterConfiguration::process(current)");
		currentConfig->process(frameCount);
	}

	if (nextConfigReady.load(std::memory_order_acquire))
	{
		{
			PerfScope _ps("FilterConfiguration::readFloatInterleaved(next)");
			nextConfig->readFloatInterleaved(input, frameCount);
		}
		{
			PerfScope _ps("FilterConfiguration::process(next)");
			nextConfig->process(frameCount);
		}
		PerfScope _ps("FilterConfiguration::doTransition");
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength, transitionFactorTable.data());
	}

	{
		PerfScope _ps("FilterConfiguration::writeFloatInterleaved");
		currentConfig->writeFloatInterleaved(output, frameCount);
	}

	finishTransitionIfReady();
}

// Process non-interleaved audio (float**)
void FilterEngine::process(float** output, float** input, unsigned frameCount)
{
	PerfScope _eapo_total("FilterEngine::process(float planar)");
	MxcsrFtzDazGuard _mxcsrGuard;

	if (currentConfig->isEmpty() && !nextConfigReady.load(std::memory_order_acquire))
	{
		// Bypass mode
		if (realChannelCount == outputChannelCount && input != output) {
			for (unsigned c = 0; c < realChannelCount; c++)
				std::copy_n(input[c], frameCount, output[c]);
		}
		return;
	}

	// Fused float32 -> double directly into planar storage. The previous path
	// converted into inputBuf2D and then copied that into the configuration; we
	// skip the intermediate buffer entirely.
	{
		PerfScope _ps("FilterConfiguration::readFloatPlanar(current)");
		currentConfig->readFloatPlanar(input, frameCount);
	}
	{
		PerfScope _ps("FilterConfiguration::process(current)");
		currentConfig->process(frameCount);
	}

	if (nextConfigReady.load(std::memory_order_acquire))
	{
		{
			PerfScope _ps("FilterConfiguration::readFloatPlanar(next)");
			nextConfig->readFloatPlanar(input, frameCount);
		}
		{
			PerfScope _ps("FilterConfiguration::process(next)");
			nextConfig->process(frameCount);
		}
		PerfScope _ps("FilterConfiguration::doTransition");
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength, transitionFactorTable.data());
	}

	{
		PerfScope _ps("FilterConfiguration::writeFloatPlanar");
		currentConfig->writeFloatPlanar(output, frameCount);
	}

	finishTransitionIfReady();
}

// Process interleaved audio (double*) - native double precision without conversion
void FilterEngine::process(double* output, double* input, unsigned frameCount)
{
	PerfScope _eapo_total("FilterEngine::process(double interleaved)");
	MxcsrFtzDazGuard _mxcsrGuard;

	if (currentConfig->isEmpty() && !nextConfigReady.load(std::memory_order_acquire))
	{
		// Bypass mode: if no filters are active, just copy input to output if necessary.
		if (realChannelCount == outputChannelCount && input != output) {
			std::copy_n(input, outputChannelCount * frameCount, output);
		}
		return;
	}

	{
		PerfScope _ps("FilterConfiguration::read(interleaved)");
		currentConfig->read(input, frameCount);
	}
	{
		PerfScope _ps("FilterConfiguration::process(current)");
		currentConfig->process(frameCount);
	}

	if (nextConfigReady.load(std::memory_order_acquire))
	{
		{
			PerfScope _ps("FilterConfiguration::read(interleaved)");
			nextConfig->read(input, frameCount);
		}
		{
			PerfScope _ps("FilterConfiguration::process(next)");
			nextConfig->process(frameCount);
		}
		PerfScope _ps("FilterConfiguration::doTransition");
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength, transitionFactorTable.data());
	}

	{
		PerfScope _ps("FilterConfiguration::write(interleaved)");
		currentConfig->write(output, frameCount);
	}

	finishTransitionIfReady();
}

// Process non-interleaved audio (double**) - native double precision without conversion
void FilterEngine::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _eapo_total("FilterEngine::process(double planar)");
	MxcsrFtzDazGuard _mxcsrGuard;

	if (currentConfig->isEmpty() && !nextConfigReady.load(std::memory_order_acquire))
	{
		// Bypass mode
		if (realChannelCount == outputChannelCount && input != output) {
			for (unsigned c = 0; c < realChannelCount; c++)
				std::copy_n(input[c], frameCount, output[c]);
		}
		return;
	}

	{
		PerfScope _ps("FilterConfiguration::read(planar)");
		currentConfig->read(input, frameCount);
	}
	{
		PerfScope _ps("FilterConfiguration::process(current)");
		currentConfig->process(frameCount);
	}

	if (nextConfigReady.load(std::memory_order_acquire))
	{
		{
			PerfScope _ps("FilterConfiguration::read(planar)");
			nextConfig->read(input, frameCount);
		}
		{
			PerfScope _ps("FilterConfiguration::process(next)");
			nextConfig->process(frameCount);
		}
		PerfScope _ps("FilterConfiguration::doTransition");
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength, transitionFactorTable.data());
	}

	{
		PerfScope _ps("FilterConfiguration::write(planar)");
		currentConfig->write(output, frameCount);
	}

	finishTransitionIfReady();
}
#pragma AVRT_CODE_END
