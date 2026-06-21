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

#include "helpers/MemoryHelper.h"
#include "helpers/LogHelper.h"
#include "DelayFilter.h"
#include "helpers/PerfProfile.h"

using std::vector;
using std::wstring;

DelayFilter::DelayFilter(double delay, bool isMs)
	: delay(delay), isMs(isMs)
{
	buffers = nullptr;
}

DelayFilter::~DelayFilter()
{
	cleanup();
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
	cleanup();

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

	// MemoryHelper::alloc returns nullptr on failure. Check every result: a
	// failed allocation here used to be dereferenced immediately, turning an
	// out-of-memory request (reachable from a config Delay value) into a crash.
	// On failure leave buffers == nullptr; process() then passes audio through
	// undelayed instead of touching a null ring buffer.
	buffers = (double**)MemoryHelper::alloc(sizeof(double*) * channelCount);
	if (buffers == nullptr)
	{
		LogFStatic(L"Delay pointer-array allocation failed (%u channels); passing audio through", channelCount);
		bufferOffset = 0;
		return channelNames;
	}

	for (unsigned i = 0; i < channelCount; i++)
	{
		buffers[i] = static_cast<double*>(MemoryHelper::alloc(sizeof *buffers[i] * bufferLength));
		if (buffers[i] == nullptr)
		{
			LogFStatic(L"Delay buffer allocation failed (%u samples); passing audio through", bufferLength);
			for (unsigned j = 0; j < i; j++)
				MemoryHelper::free(buffers[j]);
			MemoryHelper::free(buffers);
			buffers = nullptr;
			bufferOffset = 0;
			return channelNames;
		}
		std::fill_n(buffers[i], bufferLength, 0.0);
	}

	bufferOffset = 0;

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void DelayFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("DelayFilter::process");
	if (buffers == nullptr)
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
		double* bufferChannel = buffers[i];

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

void DelayFilter::cleanup()
{
	if (buffers != nullptr)
	{
		for (unsigned i = 0; i < channelCount; i++)
			MemoryHelper::free(buffers[i]);

		MemoryHelper::free(buffers);
		buffers = nullptr;
	}
}

bool DelayFilter::getIsMs() const
{
	return isMs;
}

double DelayFilter::getDelay() const
{
	return delay;
}
