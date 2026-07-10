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
#include <algorithm>

#include "helpers/PerfProfile.h"
#include "helpers/MxcsrGuard.h"
#include "FilterEngine.h"
// Filter factory headers intentionally omitted: the factories self-register and
// are pulled into the link via /WHOLEARCHIVE in the consumers; this hot-path TU
// names none of them (see FilterEngine.Configuration.cpp). The muparserx and
// registry/file helpers are omitted too: this TU never touches the parser or
// configuration loading.

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

#pragma AVRT_CODE_BEGIN
void convertFloatToDouble(double* dest, const float* src, size_t count) {
	// Promote float -> double (exact). One portable Highway loop; NEON on ARM64.
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
	// Demote double -> float (round to nearest even, same as
	// static_cast<float>). One portable Highway loop, NEON on ARM64.
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

namespace
{
	// Per-layout I/O policy for FilterEngine::processImpl below. Only the bypass
	// copy and the configuration read/write entry points differ per layout;
	// the hot-swap/transition choreography lives once in processImpl. The
	// PerfScope labels are carried per trait (the double paths deliberately
	// use the same read label for the current and the next configuration).
	struct FloatInterleavedIo
	{
		static constexpr const char* totalLabel = "FilterEngine::process(float interleaved)";
		// Fused float32 -> double + deinterleave directly into planar storage.
		static constexpr const char* readCurrentLabel = "FilterConfiguration::readFloatInterleaved(current)";
		static constexpr const char* readNextLabel = "FilterConfiguration::readFloatInterleaved(next)";
		static constexpr const char* writeLabel = "FilterConfiguration::writeFloatInterleaved";

		static void bypassCopy(float* output, float* input, unsigned outputChannelCount, unsigned, unsigned frameCount)
		{
			std::copy_n(input, outputChannelCount * frameCount, output);
		}
		static void read(FilterConfiguration& config, float* input, unsigned frameCount)
		{
			config.readFloatInterleaved(input, frameCount);
		}
		static void write(FilterConfiguration& config, float* output, unsigned frameCount)
		{
			config.writeFloatInterleaved(output, frameCount);
		}
	};

	struct FloatPlanarIo
	{
		static constexpr const char* totalLabel = "FilterEngine::process(float planar)";
		// Fused float32 -> double directly into planar storage; no intermediate
		// copy.
		static constexpr const char* readCurrentLabel = "FilterConfiguration::readFloatPlanar(current)";
		static constexpr const char* readNextLabel = "FilterConfiguration::readFloatPlanar(next)";
		static constexpr const char* writeLabel = "FilterConfiguration::writeFloatPlanar";

		static void bypassCopy(float** output, float** input, unsigned, unsigned realChannelCount, unsigned frameCount)
		{
			for (unsigned c = 0; c < realChannelCount; c++)
				std::copy_n(input[c], frameCount, output[c]);
		}
		static void read(FilterConfiguration& config, float** input, unsigned frameCount)
		{
			config.readFloatPlanar(input, frameCount);
		}
		static void write(FilterConfiguration& config, float** output, unsigned frameCount)
		{
			config.writeFloatPlanar(output, frameCount);
		}
	};

	struct DoubleInterleavedIo
	{
		static constexpr const char* totalLabel = "FilterEngine::process(double interleaved)";
		static constexpr const char* readCurrentLabel = "FilterConfiguration::read(interleaved)";
		static constexpr const char* readNextLabel = "FilterConfiguration::read(interleaved)";
		static constexpr const char* writeLabel = "FilterConfiguration::write(interleaved)";

		static void bypassCopy(double* output, double* input, unsigned outputChannelCount, unsigned, unsigned frameCount)
		{
			std::copy_n(input, outputChannelCount * frameCount, output);
		}
		static void read(FilterConfiguration& config, double* input, unsigned frameCount)
		{
			config.read(input, frameCount);
		}
		static void write(FilterConfiguration& config, double* output, unsigned frameCount)
		{
			config.write(output, frameCount);
		}
	};

	struct DoublePlanarIo
	{
		static constexpr const char* totalLabel = "FilterEngine::process(double planar)";
		static constexpr const char* readCurrentLabel = "FilterConfiguration::read(planar)";
		static constexpr const char* readNextLabel = "FilterConfiguration::read(planar)";
		static constexpr const char* writeLabel = "FilterConfiguration::write(planar)";

		static void bypassCopy(double** output, double** input, unsigned, unsigned realChannelCount, unsigned frameCount)
		{
			for (unsigned c = 0; c < realChannelCount; c++)
				std::copy_n(input[c], frameCount, output[c]);
		}
		static void read(FilterConfiguration& config, double** input, unsigned frameCount)
		{
			config.read(input, frameCount);
		}
		static void write(FilterConfiguration& config, double** output, unsigned frameCount)
		{
			config.write(output, frameCount);
		}
	};
}

// The single hot-path choreography behind all four public overloads: null and
// empty-config bypass, current-config pass, crossfade into a pending next
// configuration, write-back, transition bookkeeping. Validated against the
// audio regression references and EngineOrchestrationTests, which pin the
// crossfade behavior sample-by-sample.
template <typename IoTraits, typename SampleType>
void FilterEngine::processImpl(SampleType output, SampleType input, unsigned frameCount)
{
	PerfScope _eapo_total(IoTraits::totalLabel);
	MxcsrFtzDazGuard _mxcsrGuard;

	if (currentConfig == nullptr)
	{
		// initialize() can finish without loading any configuration (unreadable
		// ConfigPath with no custom path, or an empty path value), and the APO
		// can call process() before initialize(). Treat both like the
		// empty-config bypass below instead of dereferencing null.
		if (realChannelCount == outputChannelCount && input != output)
			IoTraits::bypassCopy(output, input, outputChannelCount, realChannelCount, frameCount);
		return;
	}

	if (currentConfig->isEmpty() && !nextConfigReady.load(std::memory_order_acquire))
	{
		// Bypass mode: if no filters are active, just copy input to output if necessary.
		if (realChannelCount == outputChannelCount && input != output)
			IoTraits::bypassCopy(output, input, outputChannelCount, realChannelCount, frameCount);
		return;
	}

	{
		PerfScope _ps(IoTraits::readCurrentLabel);
		IoTraits::read(*currentConfig, input, frameCount);
	}
	{
		PerfScope _ps("FilterConfiguration::process(current)");
		currentConfig->process(frameCount);
	}

	if (nextConfigReady.load(std::memory_order_acquire))
	{
		{
			PerfScope _ps(IoTraits::readNextLabel);
			IoTraits::read(*nextConfig, input, frameCount);
		}
		{
			PerfScope _ps("FilterConfiguration::process(next)");
			nextConfig->process(frameCount);
		}
		PerfScope _ps("FilterConfiguration::doTransition");
		transitionCounter = currentConfig->doTransition(nextConfig.get(), frameCount, transitionCounter, transitionLength, transitionFactorTable.data());
	}

	{
		PerfScope _ps(IoTraits::writeLabel);
		IoTraits::write(*currentConfig, output, frameCount);
	}

	finishTransitionIfReady();
}

// Process interleaved audio (float*)
void FilterEngine::process(float* output, float* input, unsigned frameCount)
{
	processImpl<FloatInterleavedIo>(output, input, frameCount);
}

// Process non-interleaved audio (float**)
void FilterEngine::process(float** output, float** input, unsigned frameCount)
{
	processImpl<FloatPlanarIo>(output, input, frameCount);
}

// Process interleaved audio (double*) - native double precision without conversion
void FilterEngine::process(double* output, double* input, unsigned frameCount)
{
	processImpl<DoubleInterleavedIo>(output, input, frameCount);
}

// Process non-interleaved audio (double**) - native double precision without conversion
void FilterEngine::process(double** output, double** input, unsigned frameCount)
{
	processImpl<DoublePlanarIo>(output, input, frameCount);
}
#pragma AVRT_CODE_END
