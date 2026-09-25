/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <vector>

#include "runtime/memory/AlignedMemory.h"

// A fixed delay of length() samples on a set of channels sharing one write
// position: the ring buffer the Delay filter, the VST latency compensation and
// Hilbert's aligned channels each wrote by hand (audit #348 A10/TD-67).
// allocate() is the only allocation; process() is real-time safe.
class DelayLine
{
public:
	// channelCount rings of `length` samples, zeroed, plus scratch for in-place
	// blocks up to maxFrameCount. false on allocation failure (then empty()).
	bool allocate(unsigned channelCount, unsigned length, unsigned maxFrameCount);
	void release();
	bool empty() const;
	unsigned length() const;
	// output[c] = input[c] delayed by length() samples, for c < channelCount
	// given to allocate(). output[c] may equal input[c]. Advances the shared
	// write position once per call.
	void process(double* const* output, const double* const* input, unsigned frameCount);

private:
	// One block of at most scratchCapacity frames, starting frameOffset
	// samples into every channel's buffers.
	void processBlock(double* const* output, const double* const* input, unsigned frameOffset, unsigned frameCount);

	std::vector<AlignedMemory::UniqueAllocation<double>> rings;
	AlignedMemory::UniqueAllocation<double> scratch;
	unsigned scratchCapacity = 0;
	unsigned ringLength = 0;
	unsigned writeOffset = 0;
};
