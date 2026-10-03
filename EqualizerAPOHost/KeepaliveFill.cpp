/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EqualizerAPOHost/KeepaliveFill.h"

#include <cmath>
#include <cstring>
#include <ks.h>
#include <ksmedia.h>

namespace
{
	struct SampleFormat
	{
		bool floating = false;
		unsigned validBits = 0;
	};

	bool describe(const WAVEFORMATEX& format, SampleFormat& sample)
	{
		sample.validBits = format.wBitsPerSample;
		if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			if (format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
				return false;
			const auto& ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
			if (ext.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
				sample.floating = true;
			else if (ext.SubFormat != KSDATAFORMAT_SUBTYPE_PCM)
				return false;
			if (ext.Samples.wValidBitsPerSample != 0)
				sample.validBits = ext.Samples.wValidBitsPerSample;
		}
		else if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
			sample.floating = true;
		else if (format.wFormatTag != WAVE_FORMAT_PCM)
			return false;

		if (sample.floating ? (format.wBitsPerSample != 32 || sample.validBits != 32)
			: (format.wBitsPerSample != 16 && format.wBitsPerSample != 24 && format.wBitsPerSample != 32))
			return false;
		return sample.validBits > 0 && sample.validBits <= format.wBitsPerSample
			&& format.nChannels != 0 && format.nSamplesPerSec != 0
			&& format.nBlockAlign == static_cast<unsigned>(format.nChannels) * (format.wBitsPerSample / 8);
	}

	double uniform(uint32_t& state)
	{
		if (state == 0)
			state = 0x6d2b79f5u;
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<double>(state >> 8) / 16777216.0;
	}
}

bool keepaliveFormatSupported(const WAVEFORMATEX& format)
{
	SampleFormat sample;
	return describe(format, sample);
}

void fillKeepalive(BYTE* data, UINT32 frames, const WAVEFORMATEX& format, bool dither, uint32_t& rngState)
{
	if (frames == 0 || data == nullptr)
		return;
	std::memset(data, 0, static_cast<size_t>(frames) * format.nBlockAlign);
	SampleFormat sample;
	if (!dither || !describe(format, sample))
		return;

	const unsigned bytes = format.wBitsPerSample / 8;
	const size_t count = static_cast<size_t>(frames) * format.nChannels;
	for (size_t i = 0; i < count; i++)
	{
		const double first = uniform(rngState);
		const double noise = (first - uniform(rngState)) * 1e-5;
		BYTE* target = data + i * bytes;
		if (sample.floating)
		{
			const float value = static_cast<float>(noise);
			std::memcpy(target, &value, sizeof(value));
		}
		else
		{
			const double scaled = noise * std::ldexp(1.0, static_cast<int>(sample.validBits) - 1);
			const double lower = std::floor(scaled);
			const int32_t value = static_cast<int32_t>(lower + (uniform(rngState) < scaled - lower ? 1.0 : 0.0));
			// Valid PCM bits are left-aligned; shift unsigned to avoid shifting
			// a negative signed value. Store little-endian, including packed 24.
			const uint32_t packed = static_cast<uint32_t>(value) << (format.wBitsPerSample - sample.validBits);
			for (unsigned b = 0; b < bytes; b++)
				target[b] = static_cast<BYTE>(packed >> (b * 8));
		}
	}
}
