/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// The base64 text a VSTPlugin config line stores a plug-in chunk as. Shared by
// the VST2 and VST3 state code (VST2Instance.cpp, VST3Instance.State.cpp).

#pragma once

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "platform/windows/Win32Resource.h"
#include <wincrypt.h>

namespace vstchunk
{
	inline bool decodeBase64(const std::wstring& encoded, std::vector<char>& decoded)
	{
		// CryptStringToBinary accepts even one base64 digit and invents a byte.
		// testStateRestoreFailureIsReported truncates a chunk to that impossible
		// final quantum; report it instead of handing defaults to the plugin
		// (audit #467 TD-36). Unpadded two/three-digit tails remain accepted.
		size_t digits = 0;
		for (wchar_t value : encoded)
		{
			if (value == L'=')
				break;
			if (value != L' ' && value != L'\r' && value != L'\n' && value != L'\t')
				digits++;
		}
		if (digits % 4 == 1)
			return false;
		DWORD requiredSize = 0;
		if (CryptStringToBinaryW(
			encoded.c_str(),
			0,
			CRYPT_STRING_BASE64,
			NULL,
			&requiredSize,
			NULL,
			NULL) != TRUE)
		{
			return false;
		}

		std::vector<char> result(requiredSize);
		DWORD actualSize = requiredSize;
		if (requiredSize != 0 && CryptStringToBinaryW(
			encoded.c_str(),
			0,
			CRYPT_STRING_BASE64,
			reinterpret_cast<BYTE*>(result.data()),
			&actualSize,
			NULL,
			NULL) != TRUE)
		{
			return false;
		}

		result.resize(actualSize);
		decoded = std::move(result);
		return true;
	}

	inline bool encodeBase64(const void* data, size_t size, std::wstring& encoded)
	{
		if (size > (std::numeric_limits<DWORD>::max)())
			return false;

		const DWORD binarySize = static_cast<DWORD>(size);
		DWORD requiredLength = 0;
		if (CryptBinaryToStringW(
			static_cast<const BYTE*>(data),
			binarySize,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			NULL,
			&requiredLength) != TRUE)
		{
			return false;
		}

		std::vector<wchar_t> result(requiredLength);
		DWORD actualLength = requiredLength;
		if (requiredLength == 0 || CryptBinaryToStringW(
			static_cast<const BYTE*>(data),
			binarySize,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			result.data(),
			&actualLength) != TRUE)
		{
			return false;
		}

		encoded.assign(result.data());
		return true;
	}
}
