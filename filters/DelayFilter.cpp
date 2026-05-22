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
#include "DelayFilter.h"

using namespace std;

DelayFilter::DelayFilter(double delay, bool isMs)
	: delay(delay), isMs(isMs)
{
	buffers = nullptr;
}

DelayFilter::~DelayFilter()
{
	cleanup();
}

vector<wstring> DelayFilter::initialize(float sampleRate, unsigned maxFrameCount, vector<wstring> channelNames)
{
	cleanup();

	channelCount = (unsigned)channelNames.size();

	if (isMs)
		bufferLength = (unsigned)(sampleRate * delay / 1000.0 + 0.5);
	else
		bufferLength = (unsigned)(delay + 0.5);

	buffers = (double**)MemoryHelper::alloc(sizeof(double*) * channelCount);

	for (unsigned i = 0; i < channelCount; i++)
	{
		buffers[i] = static_cast<double*>(MemoryHelper::alloc(sizeof *buffers[i] * bufferLength));
		std::fill_n(buffers[i], bufferLength, 0.0);
	}

	bufferOffset = 0;

	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void DelayFilter::process(double** output, double** input, unsigned frameCount)
{
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
