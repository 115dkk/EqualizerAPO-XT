/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Conversion between ASIO sample buffers and the double planes the processor
	seam works in. The wrapper converts at its edge. By default the DAW sees
	the target's own sample types; a per-entry option instead presents
	Float64LSB buffers and converts them to the target's types.

	Integer types scale to [-1, 1) by their nominal full scale; double to
	integer clamps to [-1, 1] and rounds to nearest. The 32-bit "aligned" types
	(Int32LSB16..24) carry their valid bits right-aligned in the low bits, the
	reading PortAudio settled on. The MSB (big-endian) types are byte-swapped
	on both sides. DSD has no PCM meaning and is rejected: findSampleCodec
	answers false and the wrapper refuses createBuffers.
*/

#pragma once

#include <cstdint>

namespace eapo::asio
{
	struct SampleCodec
	{
		long type = -1;                 // ASIOSampleType
		unsigned bytesPerSample = 0;
		void (*toDouble)(const void* source, double* destination, unsigned count) = nullptr;
		void (*fromDouble)(const double* source, void* destination, unsigned count) = nullptr;
	};

	// False for DSD and unknown types; the codec is left untouched.
	bool findSampleCodec(long asioSampleType, SampleCodec& codec) noexcept;
}
