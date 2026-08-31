/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "asio/SampleCodec.h"

#include <bit>
#include <cmath>
#include <cstring>

#include "asio/AsioSdk.h"

namespace eapo::asio
{
	namespace
	{
		constexpr float int16Scale = 1.0f / 32768.0f;
		constexpr float int24Scale = 1.0f / 8388608.0f;
		constexpr double int32Scale = 1.0 / 2147483648.0;

		inline float clampUnit(float value) noexcept
		{
			if (value > 1.0f)
				return 1.0f;
			if (value < -1.0f)
				return -1.0f;
			return value;
		}

		inline int32_t roundToInt(double value) noexcept
		{
			return static_cast<int32_t>(std::lrint(value));
		}

		inline uint16_t swap16(uint16_t value) noexcept
		{
			return static_cast<uint16_t>((value >> 8) | (value << 8));
		}

		inline uint32_t swap32(uint32_t value) noexcept
		{
			return (value >> 24) | ((value >> 8) & 0x0000ff00u) | ((value << 8) & 0x00ff0000u) | (value << 24);
		}

		inline uint64_t swap64(uint64_t value) noexcept
		{
			return (static_cast<uint64_t>(swap32(static_cast<uint32_t>(value))) << 32) | swap32(static_cast<uint32_t>(value >> 32));
		}

		// ---- 16 bit ----
		template<bool bigEndian>
		void int16ToFloat(const void* source, float* destination, unsigned count)
		{
			const uint16_t* in = static_cast<const uint16_t*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				uint16_t raw = in[i];
				if (bigEndian)
					raw = swap16(raw);
				destination[i] = static_cast<int16_t>(raw) * int16Scale;
			}
		}

		template<bool bigEndian>
		void floatToInt16(const float* source, void* destination, unsigned count)
		{
			uint16_t* out = static_cast<uint16_t*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				int32_t value = roundToInt(static_cast<double>(clampUnit(source[i])) * 32768.0);
				if (value > 32767)
					value = 32767;
				uint16_t raw = static_cast<uint16_t>(static_cast<int16_t>(value));
				out[i] = bigEndian ? swap16(raw) : raw;
			}
		}

		// ---- 24 bit packed ----
		template<bool bigEndian>
		void int24ToFloat(const void* source, float* destination, unsigned count)
		{
			const uint8_t* in = static_cast<const uint8_t*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				const uint8_t* b = in + 3 * i;
				uint32_t raw = bigEndian
					? (static_cast<uint32_t>(b[0]) << 16) | (static_cast<uint32_t>(b[1]) << 8) | b[2]
					: (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[1]) << 8) | b[0];
				// Sign-extend from bit 23.
				int32_t value = static_cast<int32_t>(raw << 8) >> 8;
				destination[i] = value * int24Scale;
			}
		}

		template<bool bigEndian>
		void floatToInt24(const float* source, void* destination, unsigned count)
		{
			uint8_t* out = static_cast<uint8_t*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				int32_t value = roundToInt(static_cast<double>(clampUnit(source[i])) * 8388608.0);
				if (value > 8388607)
					value = 8388607;
				uint32_t raw = static_cast<uint32_t>(value) & 0x00ffffffu;
				uint8_t* b = out + 3 * i;
				if (bigEndian)
				{
					b[0] = static_cast<uint8_t>(raw >> 16);
					b[1] = static_cast<uint8_t>(raw >> 8);
					b[2] = static_cast<uint8_t>(raw);
				}
				else
				{
					b[0] = static_cast<uint8_t>(raw);
					b[1] = static_cast<uint8_t>(raw >> 8);
					b[2] = static_cast<uint8_t>(raw >> 16);
				}
			}
		}

		// ---- 32 bit, `validBits` right-aligned (32 = the plain type) ----
		template<bool bigEndian, unsigned validBits>
		void int32ToFloat(const void* source, float* destination, unsigned count)
		{
			const uint32_t* in = static_cast<const uint32_t*>(source);
			const double scale = 1.0 / static_cast<double>(1ull << (validBits - 1));
			for (unsigned i = 0; i < count; i++)
			{
				uint32_t raw = in[i];
				if (bigEndian)
					raw = swap32(raw);
				int32_t value;
				if constexpr (validBits == 32)
					value = static_cast<int32_t>(raw);
				else
					value = static_cast<int32_t>(raw << (32 - validBits)) >> (32 - validBits);
				destination[i] = static_cast<float>(value * scale);
			}
		}

		template<bool bigEndian, unsigned validBits>
		void floatToInt32(const float* source, void* destination, unsigned count)
		{
			uint32_t* out = static_cast<uint32_t*>(destination);
			const double fullScale = static_cast<double>(1ull << (validBits - 1));
			const double maxValue = fullScale - 1.0;
			for (unsigned i = 0; i < count; i++)
			{
				double scaled = static_cast<double>(clampUnit(source[i])) * fullScale;
				if (scaled > maxValue)
					scaled = maxValue;
				int32_t value = roundToInt(scaled);
				uint32_t raw = static_cast<uint32_t>(value);
				if constexpr (validBits != 32)
					raw &= (1u << validBits) - 1u;
				out[i] = bigEndian ? swap32(raw) : raw;
			}
		}

		// ---- float ----
		template<bool bigEndian>
		void float32ToFloat(const void* source, float* destination, unsigned count)
		{
			if (!bigEndian)
			{
				std::memcpy(destination, source, sizeof(float) * count);
				return;
			}
			const uint32_t* in = static_cast<const uint32_t*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				uint32_t raw = swap32(in[i]);
				destination[i] = std::bit_cast<float>(raw);
			}
		}

		template<bool bigEndian>
		void floatToFloat32(const float* source, void* destination, unsigned count)
		{
			if (!bigEndian)
			{
				std::memcpy(destination, source, sizeof(float) * count);
				return;
			}
			uint32_t* out = static_cast<uint32_t*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				uint32_t raw = std::bit_cast<uint32_t>(source[i]);
				out[i] = swap32(raw);
			}
		}

		template<bool bigEndian>
		void float64ToFloat(const void* source, float* destination, unsigned count)
		{
			const uint64_t* in = static_cast<const uint64_t*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				uint64_t raw = in[i];
				if (bigEndian)
					raw = swap64(raw);
				double value = std::bit_cast<double>(raw);
				destination[i] = static_cast<float>(value);
			}
		}

		template<bool bigEndian>
		void floatToFloat64(const float* source, void* destination, unsigned count)
		{
			uint64_t* out = static_cast<uint64_t*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				double value = source[i];
				uint64_t raw = std::bit_cast<uint64_t>(value);
				out[i] = bigEndian ? swap64(raw) : raw;
			}
		}

		struct Entry
		{
			long type = -1;
			SampleCodec codec;
		};

		const Entry entries[] = {
			{ASIOSTInt16LSB, {ASIOSTInt16LSB, 2, &int16ToFloat<false>, &floatToInt16<false>}},
			{ASIOSTInt16MSB, {ASIOSTInt16MSB, 2, &int16ToFloat<true>, &floatToInt16<true>}},
			{ASIOSTInt24LSB, {ASIOSTInt24LSB, 3, &int24ToFloat<false>, &floatToInt24<false>}},
			{ASIOSTInt24MSB, {ASIOSTInt24MSB, 3, &int24ToFloat<true>, &floatToInt24<true>}},
			{ASIOSTInt32LSB, {ASIOSTInt32LSB, 4, &int32ToFloat<false, 32>, &floatToInt32<false, 32>}},
			{ASIOSTInt32MSB, {ASIOSTInt32MSB, 4, &int32ToFloat<true, 32>, &floatToInt32<true, 32>}},
			{ASIOSTFloat32LSB, {ASIOSTFloat32LSB, 4, &float32ToFloat<false>, &floatToFloat32<false>}},
			{ASIOSTFloat32MSB, {ASIOSTFloat32MSB, 4, &float32ToFloat<true>, &floatToFloat32<true>}},
			{ASIOSTFloat64LSB, {ASIOSTFloat64LSB, 8, &float64ToFloat<false>, &floatToFloat64<false>}},
			{ASIOSTFloat64MSB, {ASIOSTFloat64MSB, 8, &float64ToFloat<true>, &floatToFloat64<true>}},
			{ASIOSTInt32LSB16, {ASIOSTInt32LSB16, 4, &int32ToFloat<false, 16>, &floatToInt32<false, 16>}},
			{ASIOSTInt32LSB18, {ASIOSTInt32LSB18, 4, &int32ToFloat<false, 18>, &floatToInt32<false, 18>}},
			{ASIOSTInt32LSB20, {ASIOSTInt32LSB20, 4, &int32ToFloat<false, 20>, &floatToInt32<false, 20>}},
			{ASIOSTInt32LSB24, {ASIOSTInt32LSB24, 4, &int32ToFloat<false, 24>, &floatToInt32<false, 24>}},
			{ASIOSTInt32MSB16, {ASIOSTInt32MSB16, 4, &int32ToFloat<true, 16>, &floatToInt32<true, 16>}},
			{ASIOSTInt32MSB18, {ASIOSTInt32MSB18, 4, &int32ToFloat<true, 18>, &floatToInt32<true, 18>}},
			{ASIOSTInt32MSB20, {ASIOSTInt32MSB20, 4, &int32ToFloat<true, 20>, &floatToInt32<true, 20>}},
			{ASIOSTInt32MSB24, {ASIOSTInt32MSB24, 4, &int32ToFloat<true, 24>, &floatToInt32<true, 24>}},
		};
	}

	bool findSampleCodec(long asioSampleType, SampleCodec& codec) noexcept
	{
		for (const Entry& entry : entries)
		{
			if (entry.type == asioSampleType)
			{
				codec = entry.codec;
				return true;
			}
		}
		return false;
	}
}
