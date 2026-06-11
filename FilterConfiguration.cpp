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
void FilterConfiguration::read(double* input, unsigned frameCount)
{
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
	case 2:
		DEINTERLEAVE_MACRO(2)
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
		READ_FLOAT_INTERLEAVED_MACRO(1)
		break;
	case 2:
		READ_FLOAT_INTERLEAVED_MACRO(2)
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
	{
		double* sampleChannel = allSamples[c];
		const float* src = input[c];
		for (size_t i = 0; i < frameCount; i++)
			sampleChannel[i] = static_cast<double>(src[i]);
	}
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
			PerfScope _ps(typeid(*filterInfo->filter).name());
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
		INTERLEAVE_MACRO(2)
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
		WRITE_FLOAT_INTERLEAVED_MACRO(1)
		break;
	case 2:
		WRITE_FLOAT_INTERLEAVED_MACRO(2)
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
	{
		const double* sampleChannel = allSamples[c];
		float* dst = output[c];
		for (size_t i = 0; i < frameCount; i++)
			dst[i] = static_cast<float>(sampleChannel[i]);
	}
}
#pragma AVRT_CODE_END

bool FilterConfiguration::isEmpty()
{
	return filterInfos.empty();
}
