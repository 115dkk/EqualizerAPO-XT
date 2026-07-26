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
#include <typeinfo>

#include "FilterEngine.h"
#include "FilterConfiguration.h"
#include "helpers/PerfProfile.h"

#include "hwy/highway.h"

namespace hn = hwy::HWY_NAMESPACE;

FilterConfiguration::FilterConfiguration(const FilterEngine* engine, std::vector<std::unique_ptr<FilterInfo>> filterInfos, unsigned allChannelCount)
{
	this->allChannelCount = allChannelCount;
	realChannelCount = engine->getRealChannelCount();
	outputChannelCount = engine->getOutputChannelCount();
	unsigned maxFrameCount = engine->getMaxFrameCount();

	allSamplesData.resize(static_cast<size_t>(allChannelCount) * maxFrameCount);
	allSamples2Data.resize(static_cast<size_t>(allChannelCount) * maxFrameCount);
	allSamples.resize(allChannelCount);
	for (size_t i = 0; i < allChannelCount; i++)
		allSamples[i] = allSamplesData.data() + i * maxFrameCount;
	allSamples2.resize(allChannelCount);
	for (size_t i = 0; i < allChannelCount; i++)
		allSamples2[i] = allSamples2Data.data() + i * maxFrameCount;
	currentSamples.resize(allChannelCount);
	currentSamples2.resize(allChannelCount);

	this->filterInfos = std::move(filterInfos);

	// Resolve the profiler's labels here, off the audio thread. typeid().name()
	// allocates inside the CRT on the first question about a type, and process()
	// asked it per filter per block whenever profiling was on.
	for (const auto& info : this->filterInfos)
	{
		if (info && info->filter)
			info->profileLabel = typeid(*info->filter).name();
	}

	allStateless = true;
	for (const auto& info : this->filterInfos)
	{
		if (info && info->filter && info->filter->producesTailFromSilentInput())
		{
			allStateless = false;
			break;
		}
	}
}

FilterConfiguration::~FilterConfiguration()
{
}

#pragma AVRT_CODE_BEGIN
namespace
{
// Fused format conversions between the APO-facing interleaved buffers and the
// engine's planar double storage, one explicit Highway kernel per channel
// count the interleaved load/store family supports (2/3/4). PromoteTo is
// exact, DemoteTo rounds to nearest-even like static_cast<float>, and the
// interleaved shuffles only move bits, so each kernel is bit-identical to the
// scalar loop it replaces; SampleIoTests pins that equality. 6/8/arbitrary
// channels stay on the strided scalar loops below: Highway has no interleaved
// op for those strides and their conversion share is small.

void promoteFloats(double* dest, const float* src, size_t count)
{
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= count; i += N)
		hn::StoreU(hn::PromoteTo(dd, hn::LoadU(df, src + i)), dd, dest + i);
	for (; i < count; i++)
		dest[i] = static_cast<double>(src[i]);
}

void demoteDoubles(float* dest, const double* src, size_t count)
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

void readFloat2(double* d0, double* d1, const float* input, size_t frameCount)
{
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= frameCount; i += N)
	{
		hn::Vec<decltype(df)> f0, f1;
		hn::LoadInterleaved2(df, input + i * 2, f0, f1);
		hn::StoreU(hn::PromoteTo(dd, f0), dd, d0 + i);
		hn::StoreU(hn::PromoteTo(dd, f1), dd, d1 + i);
	}
	for (; i < frameCount; i++)
	{
		d0[i] = static_cast<double>(input[i * 2 + 0]);
		d1[i] = static_cast<double>(input[i * 2 + 1]);
	}
}

void readFloat3(double* d0, double* d1, double* d2, const float* input, size_t frameCount)
{
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= frameCount; i += N)
	{
		hn::Vec<decltype(df)> f0, f1, f2;
		hn::LoadInterleaved3(df, input + i * 3, f0, f1, f2);
		hn::StoreU(hn::PromoteTo(dd, f0), dd, d0 + i);
		hn::StoreU(hn::PromoteTo(dd, f1), dd, d1 + i);
		hn::StoreU(hn::PromoteTo(dd, f2), dd, d2 + i);
	}
	for (; i < frameCount; i++)
	{
		d0[i] = static_cast<double>(input[i * 3 + 0]);
		d1[i] = static_cast<double>(input[i * 3 + 1]);
		d2[i] = static_cast<double>(input[i * 3 + 2]);
	}
}

void readFloat4(double* d0, double* d1, double* d2, double* d3, const float* input, size_t frameCount)
{
	const hn::ScalableTag<double> dd;
	const hn::Rebind<float, decltype(dd)> df;
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= frameCount; i += N)
	{
		hn::Vec<decltype(df)> f0, f1, f2, f3;
		hn::LoadInterleaved4(df, input + i * 4, f0, f1, f2, f3);
		hn::StoreU(hn::PromoteTo(dd, f0), dd, d0 + i);
		hn::StoreU(hn::PromoteTo(dd, f1), dd, d1 + i);
		hn::StoreU(hn::PromoteTo(dd, f2), dd, d2 + i);
		hn::StoreU(hn::PromoteTo(dd, f3), dd, d3 + i);
	}
	for (; i < frameCount; i++)
	{
		d0[i] = static_cast<double>(input[i * 4 + 0]);
		d1[i] = static_cast<double>(input[i * 4 + 1]);
		d2[i] = static_cast<double>(input[i * 4 + 2]);
		d3[i] = static_cast<double>(input[i * 4 + 3]);
	}
}

// The write direction is per-target. On x86 the kernels demote two double
// vectors per channel and Combine them into one full-width float vector
// before the interleaved store (measured 5.1x the naive loop on AVX2, 2.8x
// on SSE2). On MSVC ARM64, Highway's interleaved STORES lower below scalar
// speed in every shape we measured on the CI runner (half-width 0.99x,
// full-width Combine 0.47x), so the write kernels keep the plain scalar
// loop there — identical to the pre-vectorization behavior. Interleaved
// LOADS are fine on ARM64 (2.7x), so the read kernels stay portable.
// DemoteTo rounds each lane exactly like static_cast<float>, so output bits
// are identical on every path.
#ifndef _M_ARM64
hn::Vec<hn::ScalableTag<float>> demoteTwo(const double* src, size_t i)
{
	const hn::ScalableTag<double> dd;
	const hn::ScalableTag<float> df;
	const hn::Half<decltype(df)> dfh;
	const size_t N = hn::Lanes(dd);
	return hn::Combine(df,
		hn::DemoteTo(dfh, hn::LoadU(dd, src + i + N)),
		hn::DemoteTo(dfh, hn::LoadU(dd, src + i)));
}
#endif

void writeFloat2(float* output, const double* s0, const double* s1, size_t frameCount)
{
	size_t i = 0;
#ifndef _M_ARM64
	const hn::ScalableTag<double> dd;
	const hn::ScalableTag<float> df;
	const size_t step = 2 * hn::Lanes(dd);
	for (; i + step <= frameCount; i += step)
		hn::StoreInterleaved2(demoteTwo(s0, i), demoteTwo(s1, i), df, output + i * 2);
#endif
	for (; i < frameCount; i++)
	{
		output[i * 2 + 0] = static_cast<float>(s0[i]);
		output[i * 2 + 1] = static_cast<float>(s1[i]);
	}
}

void writeFloat3(float* output, const double* s0, const double* s1, const double* s2, size_t frameCount)
{
	size_t i = 0;
#ifndef _M_ARM64
	const hn::ScalableTag<double> dd;
	const hn::ScalableTag<float> df;
	const size_t step = 2 * hn::Lanes(dd);
	for (; i + step <= frameCount; i += step)
		hn::StoreInterleaved3(demoteTwo(s0, i), demoteTwo(s1, i), demoteTwo(s2, i), df, output + i * 3);
#endif
	for (; i < frameCount; i++)
	{
		output[i * 3 + 0] = static_cast<float>(s0[i]);
		output[i * 3 + 1] = static_cast<float>(s1[i]);
		output[i * 3 + 2] = static_cast<float>(s2[i]);
	}
}

void writeFloat4(float* output, const double* s0, const double* s1, const double* s2, const double* s3, size_t frameCount)
{
	size_t i = 0;
#ifndef _M_ARM64
	const hn::ScalableTag<double> dd;
	const hn::ScalableTag<float> df;
	const size_t step = 2 * hn::Lanes(dd);
	for (; i + step <= frameCount; i += step)
		hn::StoreInterleaved4(demoteTwo(s0, i), demoteTwo(s1, i), demoteTwo(s2, i), demoteTwo(s3, i), df, output + i * 4);
#endif
	for (; i < frameCount; i++)
	{
		output[i * 4 + 0] = static_cast<float>(s0[i]);
		output[i * 4 + 1] = static_cast<float>(s1[i]);
		output[i * 4 + 2] = static_cast<float>(s2[i]);
		output[i * 4 + 3] = static_cast<float>(s3[i]);
	}
}

void readDouble2(double* d0, double* d1, const double* input, size_t frameCount)
{
	const hn::ScalableTag<double> dd;
	const size_t N = hn::Lanes(dd);
	size_t i = 0;
	for (; i + N <= frameCount; i += N)
	{
		hn::Vec<decltype(dd)> v0, v1;
		hn::LoadInterleaved2(dd, input + i * 2, v0, v1);
		hn::StoreU(v0, dd, d0 + i);
		hn::StoreU(v1, dd, d1 + i);
	}
	for (; i < frameCount; i++)
	{
		d0[i] = input[i * 2 + 0];
		d1[i] = input[i * 2 + 1];
	}
}

void writeDouble2(double* output, const double* s0, const double* s1, size_t frameCount)
{
	size_t i = 0;
#ifndef _M_ARM64
	const hn::ScalableTag<double> dd;
	const size_t N = hn::Lanes(dd);
	for (; i + N <= frameCount; i += N)
		hn::StoreInterleaved2(hn::LoadU(dd, s0 + i), hn::LoadU(dd, s1 + i), dd, output + i * 2);
#endif
	for (; i < frameCount; i++)
	{
		output[i * 2 + 0] = s0[i];
		output[i * 2 + 1] = s1[i];
	}
}
}

void FilterConfiguration::read(double* input, unsigned frameCount)
{
	if (realChannelCount == 2)
	{
		readDouble2(allSamples[0], allSamples[1], input, frameCount);
		return;
	}
#define DEINTERLEAVE_MACRO(ccount)\
	{\
		for (size_t c = 0; c < ccount; c++)\
		{\
			double* sampleChannel = allSamples[c];\
			double* i2 = input + c;\
			for (size_t i = 0; i < frameCount; i++)\
			{\
				sampleChannel[i] = i2[i * ccount];\
			}\
		}\
	}

	switch (realChannelCount)
	{
	case 1:
		DEINTERLEAVE_MACRO(1)
		break;
	case 6:
		DEINTERLEAVE_MACRO(6)
		break;
	case 8:
		DEINTERLEAVE_MACRO(8)
		break;
	default:
		DEINTERLEAVE_MACRO(realChannelCount)
	}
}

void FilterConfiguration::read(double** input, unsigned frameCount)
{
	for (unsigned c = 0; c < realChannelCount; c++)
		std::copy_n(input[c], frameCount, allSamples[c]);
}

#define READ_FLOAT_INTERLEAVED_MACRO(ccount)\
	{\
		for (size_t c = 0; c < (ccount); c++)\
		{\
			double* sampleChannel = allSamples[c];\
			const float* src = input + c;\
			for (size_t i = 0; i < frameCount; i++)\
				sampleChannel[i] = static_cast<double>(src[i * (ccount)]);\
		}\
	}

void FilterConfiguration::readFloatInterleaved(const float* input, unsigned frameCount)
{
	switch (realChannelCount)
	{
	case 1:
		promoteFloats(allSamples[0], input, frameCount);
		break;
	case 2:
		readFloat2(allSamples[0], allSamples[1], input, frameCount);
		break;
	case 3:
		readFloat3(allSamples[0], allSamples[1], allSamples[2], input, frameCount);
		break;
	case 4:
		readFloat4(allSamples[0], allSamples[1], allSamples[2], allSamples[3], input, frameCount);
		break;
	case 6:
		READ_FLOAT_INTERLEAVED_MACRO(6)
		break;
	case 8:
		READ_FLOAT_INTERLEAVED_MACRO(8)
		break;
	default:
		READ_FLOAT_INTERLEAVED_MACRO(realChannelCount)
	}
}

#undef READ_FLOAT_INTERLEAVED_MACRO

void FilterConfiguration::readFloatPlanar(const float* const* input, unsigned frameCount)
{
	for (unsigned c = 0; c < realChannelCount; c++)
		promoteFloats(allSamples[c], input[c], frameCount);
}

void FilterConfiguration::process(unsigned frameCount)
{
	for (unsigned c = realChannelCount; c < allChannelCount; c++)
		std::fill_n(allSamples[c], frameCount, 0.0);

	// for real mono input and >= stereo output, upmix to stereo as the Windows audio system would do automatically if no APO was present
	if (realChannelCount == 1 && outputChannelCount >= 2)
		std::copy_n(allSamples[0], frameCount, allSamples[1]);

	for (const auto& filterInfoPtr : filterInfos)
	{
		FilterInfo* filterInfo = filterInfoPtr.get();
		for (size_t j = 0; j < filterInfo->inChannels.size(); j++)
			currentSamples[j] = allSamples[filterInfo->inChannels[j]];
		if (filterInfo->inPlace)
		{
			for (size_t j = 0; j < filterInfo->outChannels.size(); j++)
				currentSamples2[j] = allSamples[filterInfo->outChannels[j]];
		}
		else
		{
			for (size_t j = 0; j < filterInfo->outChannels.size(); j++)
				currentSamples2[j] = allSamples2[filterInfo->outChannels[j]];
		}

		if (PerfProfile::active())
		{
			PerfScope _ps(filterInfo->profileLabel);
			filterInfo->filter->process(currentSamples2.data(), currentSamples.data(), frameCount);
		}
		else
		{
			filterInfo->filter->process(currentSamples2.data(), currentSamples.data(), frameCount);
		}

		if (!filterInfo->inPlace)
		{
			for (size_t j = 0; j < filterInfo->outChannels.size(); j++)
				std::swap(allSamples[filterInfo->outChannels[j]], allSamples2[filterInfo->outChannels[j]]);
			std::swap(currentSamples, currentSamples2);
		}
	}
}

unsigned FilterConfiguration::doTransition(FilterConfiguration* nextConfig, unsigned frameCount, unsigned transitionCounter, unsigned transitionLength, const double* factorTable)
{
	double** currentSamples = allSamples.data();
	double** nextSamples = nextConfig->allSamples.data();

	for (unsigned f = 0; f < frameCount; f++)
	{
		double factor = (transitionCounter < transitionLength) ? factorTable[transitionCounter] : 1.0;

		for (unsigned c = 0; c < outputChannelCount; c++)
			currentSamples[c][f] = currentSamples[c][f] * (1 - factor) + nextSamples[c][f] * factor;

		transitionCounter++;
	}

	return transitionCounter;
}

void FilterConfiguration::write(double* output, unsigned frameCount)
{
#define INTERLEAVE_MACRO(ccount)\
	for (size_t c = 0; c < ccount; c++)\
	{\
		const double* sampleChannel = allSamples[c];\
		double* o2 = output + c;\
		for (unsigned i = 0; i < frameCount; i++)\
		{\
			o2[i * ccount] = sampleChannel[i];\
		}\
	}

	switch (outputChannelCount)
	{
	case 1:
		INTERLEAVE_MACRO(1)
		break;
	case 2:
		writeDouble2(output, allSamples[0], allSamples[1], frameCount);
		break;
	case 6:
		INTERLEAVE_MACRO(6)
		break;
	case 8:
		INTERLEAVE_MACRO(8)
		break;
	default:
		INTERLEAVE_MACRO(outputChannelCount)
	}
}

void FilterConfiguration::write(double** output, unsigned frameCount)
{
	for (unsigned i = 0; i < outputChannelCount; i++)
		std::copy_n(allSamples[i], frameCount, output[i]);
}

#define WRITE_FLOAT_INTERLEAVED_MACRO(ccount)\
	{\
		for (size_t c = 0; c < (ccount); c++)\
		{\
			const double* sampleChannel = allSamples[c];\
			float* dst = output + c;\
			for (size_t i = 0; i < frameCount; i++)\
				dst[i * (ccount)] = static_cast<float>(sampleChannel[i]);\
		}\
	}

void FilterConfiguration::writeFloatInterleaved(float* output, unsigned frameCount)
{
	switch (outputChannelCount)
	{
	case 1:
		demoteDoubles(output, allSamples[0], frameCount);
		break;
	case 2:
		writeFloat2(output, allSamples[0], allSamples[1], frameCount);
		break;
	case 3:
		writeFloat3(output, allSamples[0], allSamples[1], allSamples[2], frameCount);
		break;
	case 4:
		writeFloat4(output, allSamples[0], allSamples[1], allSamples[2], allSamples[3], frameCount);
		break;
	case 6:
		WRITE_FLOAT_INTERLEAVED_MACRO(6)
		break;
	case 8:
		WRITE_FLOAT_INTERLEAVED_MACRO(8)
		break;
	default:
		WRITE_FLOAT_INTERLEAVED_MACRO(outputChannelCount)
	}
}

#undef WRITE_FLOAT_INTERLEAVED_MACRO

void FilterConfiguration::writeFloatPlanar(float* const* output, unsigned frameCount)
{
	for (unsigned c = 0; c < outputChannelCount; c++)
		demoteDoubles(output[c], allSamples[c], frameCount);
}
#pragma AVRT_CODE_END

bool FilterConfiguration::isEmpty()
{
	return filterInfos.empty();
}
