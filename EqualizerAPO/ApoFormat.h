/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include <cmath>
#include <cstddef>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioenginebaseapo.h>

// The pure decisions of the APO DLL, pulled out of the COM class (audit #275
// A8/TD-28). The COM aggregation and the audiodg interaction genuinely cannot
// be tested automatically, but format detection, the silence verdict and the
// channel-mask fallback are plain functions - and they are the parts that are
// hardest to find when they go quietly wrong on a user's endpoint. They live
// in this ATL-free header so EngineOrchestrationTests can pin them.
namespace apo
{

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, spelled by value: including ks.h/ksmedia.h
// here would inject the KS GUID macro system into every consumer, where it
// breaks cguid.h (GUID_NULL becomes an __uuidof alias) in TUs that include
// COM headers afterwards.
inline constexpr GUID kIeeeFloatSubtype =
	{ 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

enum class SampleFormat
{
	Unsupported = 0,
	Float32 = 1,
	Float64 = 2
};

inline SampleFormat detectSampleFormat(const UNCOMPRESSEDAUDIOFORMAT& f)
{
	// Windows audio engine normally hands system-effect APOs IEEE_FLOAT samples
	// even when the endpoint runs an integer format underneath. We only need to
	// match the container size to pick the right reinterpret. Validating the
	// exact dwValidBitsPerSample value is too strict - some virtual devices
	// (CABLE Input, loopback adapters, etc.) report non-canonical valid-bit
	// counts even though the container is plain 32-bit float. Rejecting them
	// here would fall through to silent output and make the device sound
	// dead.
	if (IsEqualGUID(f.guidFormatType, kIeeeFloatSubtype))
	{
		if (f.dwBytesPerSampleContainer == 4)
			return SampleFormat::Float32;
		if (f.dwBytesPerSampleContainer == 8)
			return SampleFormat::Float64;
	}
	return SampleFormat::Unsupported;
}

inline size_t bytesPerSample(SampleFormat format)
{
	switch (format)
	{
	case SampleFormat::Float32: return sizeof(float);
	case SampleFormat::Float64: return sizeof(double);
	default: return 0;
	}
}

// The silence verdict processBlock() renders on a nominally-silent input
// buffer after the engine ran: BUFFER_SILENT seems to be important for some
// sound card drivers, so only report audible output if there really is audio
// above this threshold.
template<typename SampleT>
inline bool isBlockSilent(const SampleT* samples, size_t sampleCount)
{
	const SampleT threshold = static_cast<SampleT>(1e-10);
	for (size_t i = 0; i < sampleCount; i++)
	{
		if (std::abs(samples[i]) > threshold)
			return false;
	}
	return true;
}

// The endpoint's channel mask, taken from the connection this instance
// processes (input for capture, output for render), with a fallback to the
// opposite side when the preferred mask is zero and the channel counts agree -
// some drivers only fill one side in.
inline unsigned resolveChannelMask(bool capture,
	unsigned inputMask, unsigned inputChannelCount,
	unsigned outputMask, unsigned outputChannelCount)
{
	unsigned mask = capture ? inputMask : outputMask;
	if (mask == 0 && inputChannelCount == outputChannelCount)
		mask = capture ? outputMask : inputMask;
	return mask;
}

} // namespace apo
