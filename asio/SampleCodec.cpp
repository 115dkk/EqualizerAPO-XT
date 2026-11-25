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

		template<unsigned validBits>
		int32_t integerSample(double value) noexcept
		{
			if (std::isnan(value))
				value = 0.0;
			else if (value > 1.0)
				value = 1.0;
			else if (value < -1.0)
				value = -1.0;
			const double scale = static_cast<double>(UINT64_C(1) << (validBits - 1));
			const double maximum = scale - 1.0;
			double scaled = value * scale;
			if (scaled > maximum)
				scaled = maximum;
			return static_cast<int32_t>(std::lrint(scaled));
		}

		// ---- 16 bit ----
		template<bool bigEndian>
		void int16ToDouble(const void* source, double* destination, unsigned count)
		{
			const unsigned char* in = static_cast<const unsigned char*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				uint16_t raw = 0;
				std::memcpy(&raw, in + static_cast<size_t>(i) * sizeof(raw), sizeof(raw));
				if (bigEndian)
					raw = swap16(raw);
				destination[i] = static_cast<double>(static_cast<int16_t>(raw)) * (1.0 / 32768.0);
			}
		}

		template<bool bigEndian>
		void doubleToInt16(const double* source, void* destination, unsigned count)
		{
			unsigned char* out = static_cast<unsigned char*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				const int32_t value = integerSample<16>(source[i]);
				uint16_t raw = static_cast<uint16_t>(static_cast<int16_t>(value));
				if (bigEndian)
					raw = swap16(raw);
				std::memcpy(out + static_cast<size_t>(i) * sizeof(raw), &raw, sizeof(raw));
			}
		}

		// ---- 24 bit packed ----
		template<bool bigEndian>
		void int24ToDouble(const void* source, double* destination, unsigned count)
		{
			const uint8_t* in = static_cast<const uint8_t*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				const uint8_t* b = in + 3 * i;
				const uint32_t raw = bigEndian
					? (static_cast<uint32_t>(b[0]) << 16) | (static_cast<uint32_t>(b[1]) << 8) | b[2]
					: (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[1]) << 8) | b[0];
				const int32_t value = (raw & 0x00800000u) != 0
					? static_cast<int32_t>(static_cast<int64_t>(raw) - INT64_C(0x1000000))
					: static_cast<int32_t>(raw);
				destination[i] = static_cast<double>(value) * (1.0 / 8388608.0);
			}
		}

		template<bool bigEndian>
		void doubleToInt24(const double* source, void* destination, unsigned count)
		{
			uint8_t* out = static_cast<uint8_t*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				const uint32_t raw = static_cast<uint32_t>(integerSample<24>(source[i])) & 0x00ffffffu;
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
		void int32ToDouble(const void* source, double* destination, unsigned count)
		{
			const unsigned char* in = static_cast<const unsigned char*>(source);
			const uint64_t modulus = UINT64_C(1) << validBits;
			const uint32_t mask = validBits == 32 ? UINT32_MAX : static_cast<uint32_t>(modulus - 1);
			const uint32_t signBit = UINT32_C(1) << (validBits - 1);
			const double scale = 1.0 / static_cast<double>(UINT64_C(1) << (validBits - 1));
			for (unsigned i = 0; i < count; i++)
			{
				uint32_t raw = 0;
				std::memcpy(&raw, in + static_cast<size_t>(i) * sizeof(raw), sizeof(raw));
				if (bigEndian)
					raw = swap32(raw);
				raw &= mask;
				const int64_t value = (raw & signBit) != 0
					? static_cast<int64_t>(raw) - static_cast<int64_t>(modulus) : static_cast<int64_t>(raw);
				destination[i] = static_cast<double>(value) * scale;
			}
		}

		template<bool bigEndian, unsigned validBits>
		void doubleToInt32(const double* source, void* destination, unsigned count)
		{
			unsigned char* out = static_cast<unsigned char*>(destination);
			const uint32_t mask = validBits == 32 ? UINT32_MAX : static_cast<uint32_t>((UINT64_C(1) << validBits) - 1);
			for (unsigned i = 0; i < count; i++)
			{
				uint32_t raw = static_cast<uint32_t>(integerSample<validBits>(source[i])) & mask;
				if (bigEndian)
					raw = swap32(raw);
				std::memcpy(out + static_cast<size_t>(i) * sizeof(raw), &raw, sizeof(raw));
			}
		}

		// ---- floating point ----
		template<bool bigEndian>
		void float32ToDouble(const void* source, double* destination, unsigned count)
		{
			const unsigned char* in = static_cast<const unsigned char*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				uint32_t raw = 0;
				std::memcpy(&raw, in + static_cast<size_t>(i) * sizeof(raw), sizeof(raw));
				if (bigEndian)
					raw = swap32(raw);
				destination[i] = static_cast<double>(std::bit_cast<float>(raw));
			}
		}

		template<bool bigEndian>
		void doubleToFloat32(const double* source, void* destination, unsigned count)
		{
			unsigned char* out = static_cast<unsigned char*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				uint32_t raw = std::bit_cast<uint32_t>(static_cast<float>(source[i]));
				if (bigEndian)
					raw = swap32(raw);
				std::memcpy(out + static_cast<size_t>(i) * sizeof(raw), &raw, sizeof(raw));
			}
		}

		template<bool bigEndian>
		void float64ToDouble(const void* source, double* destination, unsigned count)
		{
			if (!bigEndian)
			{
				std::memcpy(destination, source, sizeof(double) * count);
				return;
			}
			const unsigned char* in = static_cast<const unsigned char*>(source);
			for (unsigned i = 0; i < count; i++)
			{
				uint64_t raw = 0;
				std::memcpy(&raw, in + static_cast<size_t>(i) * sizeof(raw), sizeof(raw));
				destination[i] = std::bit_cast<double>(swap64(raw));
			}
		}

		template<bool bigEndian>
		void doubleToFloat64(const double* source, void* destination, unsigned count)
		{
			if (!bigEndian)
			{
				std::memcpy(destination, source, sizeof(double) * count);
				return;
			}
			unsigned char* out = static_cast<unsigned char*>(destination);
			for (unsigned i = 0; i < count; i++)
			{
				const uint64_t raw = swap64(std::bit_cast<uint64_t>(source[i]));
				std::memcpy(out + static_cast<size_t>(i) * sizeof(raw), &raw, sizeof(raw));
			}
		}

		struct Entry
		{
			long type = -1;
			SampleCodec codec;
		};

		const Entry entries[] = {
			{ASIOSTInt16LSB, {ASIOSTInt16LSB, 2, &int16ToDouble<false>, &doubleToInt16<false>}},
			{ASIOSTInt16MSB, {ASIOSTInt16MSB, 2, &int16ToDouble<true>, &doubleToInt16<true>}},
			{ASIOSTInt24LSB, {ASIOSTInt24LSB, 3, &int24ToDouble<false>, &doubleToInt24<false>}},
			{ASIOSTInt24MSB, {ASIOSTInt24MSB, 3, &int24ToDouble<true>, &doubleToInt24<true>}},
			{ASIOSTInt32LSB, {ASIOSTInt32LSB, 4, &int32ToDouble<false, 32>, &doubleToInt32<false, 32>}},
			{ASIOSTInt32MSB, {ASIOSTInt32MSB, 4, &int32ToDouble<true, 32>, &doubleToInt32<true, 32>}},
			{ASIOSTFloat32LSB, {ASIOSTFloat32LSB, 4, &float32ToDouble<false>, &doubleToFloat32<false>}},
			{ASIOSTFloat32MSB, {ASIOSTFloat32MSB, 4, &float32ToDouble<true>, &doubleToFloat32<true>}},
			{ASIOSTFloat64LSB, {ASIOSTFloat64LSB, 8, &float64ToDouble<false>, &doubleToFloat64<false>}},
			{ASIOSTFloat64MSB, {ASIOSTFloat64MSB, 8, &float64ToDouble<true>, &doubleToFloat64<true>}},
			{ASIOSTInt32LSB16, {ASIOSTInt32LSB16, 4, &int32ToDouble<false, 16>, &doubleToInt32<false, 16>}},
			{ASIOSTInt32LSB18, {ASIOSTInt32LSB18, 4, &int32ToDouble<false, 18>, &doubleToInt32<false, 18>}},
			{ASIOSTInt32LSB20, {ASIOSTInt32LSB20, 4, &int32ToDouble<false, 20>, &doubleToInt32<false, 20>}},
			{ASIOSTInt32LSB24, {ASIOSTInt32LSB24, 4, &int32ToDouble<false, 24>, &doubleToInt32<false, 24>}},
			{ASIOSTInt32MSB16, {ASIOSTInt32MSB16, 4, &int32ToDouble<true, 16>, &doubleToInt32<true, 16>}},
			{ASIOSTInt32MSB18, {ASIOSTInt32MSB18, 4, &int32ToDouble<true, 18>, &doubleToInt32<true, 18>}},
			{ASIOSTInt32MSB20, {ASIOSTInt32MSB20, 4, &int32ToDouble<true, 20>, &doubleToInt32<true, 20>}},
			{ASIOSTInt32MSB24, {ASIOSTInt32MSB24, 4, &int32ToDouble<true, 24>, &doubleToInt32<true, 24>}},
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
