/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Conversion between the ASIO sample types a target driver exposes and the
	float32 planes the processor seam works in. The wrapper converts at its
	edge only: the DAW keeps seeing the target's own sample types, so nothing
	about the device changes except the driver name suffix.

	Integer types scale to [-1, 1) by their nominal full scale; float to
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
		void (*toFloat)(const void* source, float* destination, unsigned count) = nullptr;
		void (*fromFloat)(const float* source, void* destination, unsigned count) = nullptr;
	};

	// False for DSD and unknown types; the codec is left untouched.
	bool findSampleCodec(long asioSampleType, SampleCodec& codec) noexcept;
}
