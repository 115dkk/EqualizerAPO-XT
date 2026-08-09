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
#include <cmath>

#include "runtime/memory/AlignedMemory.h"
#include "services/logging/Logging.h"
#include "DelayFilter.h"
#include "diagnostics/performance/PerfProfile.h"

using std::vector;
using std::wstring;

DelayFilter::DelayFilter(double delay, bool isMs)
	: delay(delay), isMs(isMs)
{
}

// Upper bound on the per-channel delay ring buffer. A config Delay value is
// attacker-influenceable (the config directory is user-writable) and otherwise
// unbounded. Casting a huge or non-finite double to unsigned is undefined
// behaviour, and an unbounded length asks for a multi-gigabyte allocation that
// crashes audiodg. 32 Mi doubles per channel (256 MiB, ~700 s at 48 kHz) is far
// beyond any real delay line, so clamping here never affects legitimate use.
static const double kMaxDelaySamples = 32.0 * 1024.0 * 1024.0;

vector<wstring> DelayFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	buffers.clear();

	channelCount = (unsigned)channelNames.size();

	double samples = isMs ? (static_cast<double>(sampleRate) * delay / 1000.0) : delay;
	samples = std::floor(samples + 0.5);
	if (!(samples >= 1.0))
		samples = 1.0;
	if (samples > kMaxDelaySamples)
	{
		LogFStatic(L"Delay length %.0f samples exceeds the %.0f sample cap; clamping", samples, kMaxDelaySamples);
		samples = kMaxDelaySamples;
	}
	bufferLength = static_cast<unsigned>(samples);

	std::vector<AlignedMemory::UniqueAllocation<double>> preparedBuffers;
	preparedBuffers.reserve(channelCount);
	for (unsigned i = 0; i < channelCount; i++)
	{
		auto buffer = AlignedMemory::allocateArray<double>(bufferLength);
		if (!buffer)
		{
			LogFStatic(L"Delay buffer allocation failed (%u samples); passing audio through", bufferLength);
			bufferOffset = 0;
			return channelNames;
		}
		std::fill_n(buffer.get(), bufferLength, 0.0);
		preparedBuffers.push_back(std::move(buffer));
	}
	buffers = std::move(preparedBuffers);

	bufferOffset = 0;

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void DelayFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("DelayFilter::process");
	if (buffers.empty())
	{
		// Allocation failed at initialize(): pass audio through undelayed rather
		// than dereferencing a null ring buffer.
		for (unsigned i = 0; i < channelCount; i++)
			if (output[i] != input[i])
				std::copy_n(input[i], frameCount, output[i]);
		return;
	}
	for (unsigned i = 0; i < channelCount; i++)
	{
		double* inputChannel = input[i];
		double* outputChannel = output[i];
		double* bufferChannel = buffers[i].get();

		if (bufferLength <= frameCount)
		{
			std::copy_n(bufferChannel + bufferOffset, bufferLength - bufferOffset, outputChannel);
			std::copy_n(bufferChannel, bufferOffset, outputChannel + bufferLength - bufferOffset);
			std::copy_n(inputChannel, frameCount - bufferLength, outputChannel + bufferLength);
			std::copy_n(inputChannel + frameCount - bufferLength, bufferLength, bufferChannel);
		}
		else
		{
			if (bufferLength < bufferOffset + frameCount)
			{
				std::copy_n(bufferChannel + bufferOffset, bufferLength - bufferOffset, outputChannel);
				std::copy_n(bufferChannel, frameCount - (bufferLength - bufferOffset), outputChannel + bufferLength - bufferOffset);
				std::copy_n(inputChannel, bufferLength - bufferOffset, bufferChannel + bufferOffset);
				std::copy_n(inputChannel + bufferLength - bufferOffset, frameCount - (bufferLength - bufferOffset), bufferChannel);
			}
			else
			{
				std::copy_n(bufferChannel + bufferOffset, frameCount, outputChannel);
				std::copy_n(inputChannel, frameCount, bufferChannel + bufferOffset);
			}
		}
	}

	if (bufferLength <= frameCount)
		bufferOffset = 0;
	else
		bufferOffset = (bufferOffset + frameCount) % bufferLength;
}
#pragma AVRT_CODE_END

bool DelayFilter::getIsMs() const
{
	return isMs;
}

double DelayFilter::getDelay() const
{
	return delay;
}
