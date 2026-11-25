/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "asio/AsioSdk.h"

namespace asiotest::oracle
{
	// Bytes per sample of the little-endian PCM types the probe offers; 0 for anything else.
	inline unsigned bytesPerSample(long asioSampleType) noexcept
	{
		switch (asioSampleType)
		{
		case ASIOSTInt16LSB:
			return 2;
		case ASIOSTInt24LSB:
			return 3;
		case ASIOSTInt32LSB:
		case ASIOSTFloat32LSB:
			return 4;
		case ASIOSTFloat64LSB:
			return 8;
		default:
			return 0;
		}
	}

	inline bool supported(long asioSampleType) noexcept
	{
		return bytesPerSample(asioSampleType) != 0;
	}

	namespace detail
	{
		inline int64_t integerSample(double value, unsigned bits) noexcept
		{
			const double scale = static_cast<double>(UINT64_C(1) << (bits - 1));
			const int64_t maximum = static_cast<int64_t>((UINT64_C(1) << (bits - 1)) - 1);
			const double clamped = (std::clamp)(value, -1.0, 1.0);
			const int64_t rounded = static_cast<int64_t>(std::nearbyint(clamped * scale));
			return (std::min)(rounded, maximum);
		}

		inline void encodeInteger(const double* source, unsigned char* destination, size_t count,
			unsigned bits, unsigned bytes) noexcept
		{
			for (size_t i = 0; i < count; i++)
			{
				const int64_t sample = integerSample(source[i], bits);
				const uint32_t packed = static_cast<uint32_t>(sample);
				std::memcpy(destination + i * bytes, &packed, bytes);
			}
		}

		inline void decodeInteger(const unsigned char* source, double* destination, size_t count,
			unsigned bits, unsigned bytes) noexcept
		{
			const uint32_t signBit = UINT32_C(1) << (bits - 1);
			const int64_t modulus = INT64_C(1) << bits;
			const double scale = static_cast<double>(UINT64_C(1) << (bits - 1));
			for (size_t i = 0; i < count; i++)
			{
				uint32_t packed = 0;
				std::memcpy(&packed, source + i * bytes, bytes);
				const int64_t sample = (packed & signBit) != 0
					? static_cast<int64_t>(packed) - modulus : static_cast<int64_t>(packed);
				destination[i] = static_cast<double>(sample) / scale;
			}
		}
	}

	// Converts doubles to little-endian PCM bytes independently of the product codec.
	inline void encode(long asioSampleType, const double* source, unsigned char* destination, size_t count) noexcept
	{
		switch (asioSampleType)
		{
		case ASIOSTInt16LSB:
			detail::encodeInteger(source, destination, count, 16, 2);
			break;
		case ASIOSTInt24LSB:
			detail::encodeInteger(source, destination, count, 24, 3);
			break;
		case ASIOSTInt32LSB:
			detail::encodeInteger(source, destination, count, 32, 4);
			break;
		case ASIOSTFloat32LSB:
			for (size_t i = 0; i < count; i++)
			{
				const float sample = static_cast<float>(source[i]);
				std::memcpy(destination + i * sizeof(sample), &sample, sizeof(sample));
			}
			break;
		case ASIOSTFloat64LSB:
			for (size_t i = 0; i < count; i++)
				std::memcpy(destination + i * sizeof(source[i]), &source[i], sizeof(source[i]));
			break;
		default:
			break;
		}
	}

	// Converts little-endian PCM bytes to doubles independently of the product codec.
	inline void decode(long asioSampleType, const unsigned char* source, double* destination, size_t count) noexcept
	{
		switch (asioSampleType)
		{
		case ASIOSTInt16LSB:
			detail::decodeInteger(source, destination, count, 16, 2);
			break;
		case ASIOSTInt24LSB:
			detail::decodeInteger(source, destination, count, 24, 3);
			break;
		case ASIOSTInt32LSB:
			detail::decodeInteger(source, destination, count, 32, 4);
			break;
		case ASIOSTFloat32LSB:
			for (size_t i = 0; i < count; i++)
			{
				float sample = 0.0f;
				std::memcpy(&sample, source + i * sizeof(sample), sizeof(sample));
				destination[i] = static_cast<double>(sample);
			}
			break;
		case ASIOSTFloat64LSB:
			for (size_t i = 0; i < count; i++)
				std::memcpy(&destination[i], source + i * sizeof(destination[i]), sizeof(destination[i]));
			break;
		default:
			break;
		}
	}

	// What a float32 seam does to a value.
	inline double throughFloat32(double value) noexcept
	{
		// This cast deliberately models the precision loss under test.
		// cppcheck-suppress suspiciousFloatingPointCast
		return static_cast<double>(static_cast<float>(value));
	}
}
