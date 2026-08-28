/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	SHA-256 over a byte range through CNG, for the probe's output digests.
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

namespace asiotest
{
	inline std::string sha256Hex(const unsigned char* data, size_t bytes)
	{
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		unsigned char digest[32] = {};
		std::string hex;
		if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
			return hex;
		if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0)
		{
			if (BCryptHashData(hash, const_cast<unsigned char*>(data), static_cast<ULONG>(bytes), 0) == 0
				&& BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
			{
				static const char* const digits = "0123456789abcdef";
				for (unsigned char byte : digest)
				{
					hex.push_back(digits[byte >> 4]);
					hex.push_back(digits[byte & 0x0f]);
				}
			}
			BCryptDestroyHash(hash);
		}
		BCryptCloseAlgorithmProvider(algorithm, 0);
		return hex;
	}

	inline std::string sha256Hex(const std::vector<unsigned char>& data)
	{
		return sha256Hex(data.data(), data.size());
	}
}
