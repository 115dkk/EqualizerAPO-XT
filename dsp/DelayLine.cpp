/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <algorithm>

#include "dsp/DelayLine.h"

bool DelayLine::allocate(unsigned channelCount, unsigned length, unsigned maxFrameCount)
{
	release();
	// Nothing to delay: stay empty. Callers only ask for a positive length.
	if (channelCount == 0 || length == 0)
		return true;

	std::vector<AlignedMemory::UniqueAllocation<double>> preparedRings;
	preparedRings.reserve(channelCount);
	for (unsigned i = 0; i < channelCount; i++)
	{
		auto ring = AlignedMemory::allocateArray<double>(length);
		if (!ring)
			return false;
		std::fill_n(ring.get(), length, 0.0);
		preparedRings.push_back(std::move(ring));
	}

	// An in-place block is copied aside before the ring overwrites it. A block
	// longer than the scratch is processed in scratch-sized pieces.
	const unsigned capacity = (std::max)(maxFrameCount, 1u);
	auto preparedScratch = AlignedMemory::allocateArray<double>(capacity);
	if (!preparedScratch)
		return false;

	rings = std::move(preparedRings);
	scratch = std::move(preparedScratch);
	scratchCapacity = capacity;
	ringLength = length;
	writeOffset = 0;
	return true;
}

void DelayLine::release()
{
	rings.clear();
	scratch.reset();
	scratchCapacity = 0;
	ringLength = 0;
	writeOffset = 0;
}

bool DelayLine::empty() const
{
	return rings.empty();
}

unsigned DelayLine::length() const
{
	return ringLength;
}

#pragma AVRT_CODE_BEGIN
void DelayLine::process(double* const* output, const double* const* input, unsigned frameCount)
{
	if (rings.empty())
		return;
	for (unsigned frameOffset = 0; frameOffset < frameCount; frameOffset += scratchCapacity)
		processBlock(output, input, frameOffset, (std::min)(scratchCapacity, frameCount - frameOffset));
}

void DelayLine::processBlock(double* const* output, const double* const* input, unsigned frameOffset, unsigned frameCount)
{
	const unsigned bufferLength = ringLength;
	const unsigned bufferOffset = writeOffset;
	for (size_t i = 0; i < rings.size(); i++)
	{
		double* outputChannel = output[i] + frameOffset;
		const double* inputChannel = input[i] + frameOffset;
		double* bufferChannel = rings[i].get();

		// The ring is read into the output before the input is written into
		// the ring, so an in-place block needs its input copied aside first.
		if (outputChannel == inputChannel)
		{
			std::copy_n(inputChannel, frameCount, scratch.get());
			inputChannel = scratch.get();
		}

		if (bufferLength <= frameCount)
		{
			std::copy_n(bufferChannel + bufferOffset, bufferLength - bufferOffset, outputChannel);
			std::copy_n(bufferChannel, bufferOffset, outputChannel + bufferLength - bufferOffset);
			std::copy_n(inputChannel, frameCount - bufferLength, outputChannel + bufferLength);
			std::copy_n(inputChannel + frameCount - bufferLength, bufferLength, bufferChannel);
		}
		else if (bufferLength < bufferOffset + frameCount)
		{
			// Wrapping around the ring
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

	if (bufferLength <= frameCount)
		writeOffset = 0;
	else
		writeOffset = (bufferOffset + frameCount) % bufferLength;
}
#pragma AVRT_CODE_END
