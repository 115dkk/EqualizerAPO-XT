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
#include "filters/ExpressionFilterFactory.h"
#include "filters/DeviceFilterFactory.h"
#include "filters/StageFilterFactory.h"
#include "filters/IfFilterFactory.h"
#include "filters/ChannelFilterFactory.h"
#include "filters/BiQuadFilterFactory.h"
#include "filters/IIRFilterFactory.h"
#include "filters/PreampFilterFactory.h"
#include "filters/DelayFilterFactory.h"
#include "filters/CopyFilterFactory.h"
#include "filters/IncludeFilterFactory.h"
#include "filters/ConvolutionFilterFactory.h"
#include "filters/GraphicEQFilterFactory.h"
#include "filters/VSTPluginFilterFactory.h"
#include "filters/loudnessCorrection/LoudnessCorrectionFilterFactory.h"

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
#if defined(__AVX512F__) && !defined(_M_ARM64) // AVX-512 Path (e.g., Zen 4, some Intel CPUs)
	size_t i = 0;
	for (; i + 16 <= count; i += 16) {
		// Load 16 floats
		__m512 float_vec = _mm512_loadu_ps(src + i);
		// Convert the lower 8 floats to 8 doubles
		__m512d double_vec_lo = _mm512_cvtps_pd(_mm512_extractf32x8_ps(float_vec, 0));
		// Convert the upper 8 floats to 8 doubles
		__m512d double_vec_hi = _mm512_cvtps_pd(_mm512_extractf32x8_ps(float_vec, 1));
		// Store the 16 resulting doubles
		_mm512_storeu_pd(dest + i, double_vec_lo);
		_mm512_storeu_pd(dest + i + 8, double_vec_hi);
	}
	// Handle any remaining elements
	for (; i < count; ++i) dest[i] = static_cast<double>(src[i]);
#elif defined(__AVX2__) && !defined(_M_ARM64) // AVX2 / AVX Fallback Path (e.g., Zen 2/3)
	size_t i = 0;
	for (; i + 8 <= count; i += 8) {
		// Load 8 floats into a 256-bit register
		__m256 float_vec = _mm256_loadu_ps(src + i);
		// Convert the lower 4 floats to 4 doubles
		__m256d double_vec_lo = _mm256_cvtps_pd(_mm256_extractf128_ps(float_vec, 0));
		// Convert the upper 4 floats to 4 doubles
		__m256d double_vec_hi = _mm256_cvtps_pd(_mm256_extractf128_ps(float_vec, 1));
		// Store the 8 resulting doubles
		_mm256_storeu_pd(dest + i, double_vec_lo);
		_mm256_storeu_pd(dest + i + 4, double_vec_hi);
	}
	// Handle any remaining elements
	for (; i < count; ++i) dest[i] = static_cast<double>(src[i]);
#else // Scalar fallback for non-x86 or very old CPUs
	for (size_t i = 0; i < count; ++i) dest[i] = static_cast<double>(src[i]);
#endif
}

// Converts a block of doubles back to floats.
void convertDoubleToFloat(float* dest, const double* src, size_t count) {
#if defined(__AVX512F__) && !defined(_M_ARM64) // AVX-512 Path
	size_t i = 0;
	for (; i + 16 <= count; i += 16) {
		// Load 16 doubles from memory
		__m512d double_vec_lo = _mm512_loadu_pd(src + i);
		__m512d double_vec_hi = _mm512_loadu_pd(src + i + 8);
		// Convert 8 doubles to 8 floats
		__m256 float_vec_lo = _mm512_cvtpd_ps(double_vec_lo);
		// Convert another 8 doubles to 8 floats
		__m256 float_vec_hi = _mm512_cvtpd_ps(double_vec_hi);
		// Combine the two 256-bit float vectors into one 512-bit vector
		__m512 float_vec = _mm512_insertf32x8(_mm512_castps256_ps512(float_vec_lo), float_vec_hi, 1);
		_mm512_storeu_ps(dest + i, float_vec);
	}
	for (; i < count; ++i) dest[i] = static_cast<float>(src[i]);
#elif defined(__AVX2__) && !defined(_M_ARM64) // AVX2 / AVX Fallback Path
	size_t i = 0;
	for (; i + 8 <= count; i += 8) {
		// Load 8 doubles from memory
		__m256d double_vec_lo = _mm256_loadu_pd(src + i);
		__m256d double_vec_hi = _mm256_loadu_pd(src + i + 4);
		// Convert 4 doubles to 4 floats
		__m128 float_vec_lo = _mm256_cvtpd_ps(double_vec_lo);
		// Convert another 4 doubles to 4 floats
		__m128 float_vec_hi = _mm256_cvtpd_ps(double_vec_hi);
		// Combine the two 128-bit float vectors into one 256-bit vector
		__m256 float_vec = _mm256_insertf128_ps(_mm256_castps128_ps256(float_vec_lo), float_vec_hi, 1);
		_mm256_storeu_ps(dest + i, float_vec);
	}
	for (; i < count; ++i) dest[i] = static_cast<float>(src[i]);
#else // Scalar fallback
	for (size_t i = 0; i < count; ++i) dest[i] = static_cast<float>(src[i]);
#endif
}


// Process interleaved audio (float*)
void FilterEngine::process(float* output, float* input, unsigned frameCount)
{
	PerfScope _eapo_total("FilterEngine::process(float interleaved)");
	MxcsrFtzDazGuard _mxcsrGuard;

	if (currentConfig->isEmpty() && !nextConfig)
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

	if (nextConfig)
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

	if (currentConfig->isEmpty() && !nextConfig)
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

	if (nextConfig)
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

	if (currentConfig->isEmpty() && !nextConfig)
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

	if (nextConfig)
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

	if (currentConfig->isEmpty() && !nextConfig)
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

	if (nextConfig)
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
