/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	ASIO driver names are narrow strings in the process's ANSI code page (the
	WASAPI target narrows an endpoint's friendly name with CP_ACP, a vendor
	driver writes whatever its installer wrote). Two conversions follow from
	that, both taking the code page so AsioTests can pin them with a fixed one:
	widening a name for the engine and the host, and cutting a name to fit the
	32-byte getDriverName buffer without leaving half a double-byte character
	(audit #348 TD-10: the wrapper widened byte by byte, so a Korean endpoint
	name reached the engine's device string, the host log and the HKCU
	DeviceName fact as Latin-1 garbage, and a "Device:" line naming it never
	matched). Header-only so the four projects compiling the asio core need no
	new source entry.
*/

#pragma once

#include <cstddef>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace eapo::asio::drivername
{
	// The largest n <= maxBytes such that text[0, n) ends on a character
	// boundary of `codePage` (a lead byte is never the last byte kept).
	inline size_t characterBoundary(const char* text, size_t length, size_t maxBytes, unsigned codePage) noexcept
	{
		if (length <= maxBytes)
			return length;
		size_t i = 0;
		while (i < length)
		{
			const size_t step = (i + 1 < length && IsDBCSLeadByteEx(codePage, static_cast<BYTE>(text[i]))) ? 2 : 1;
			if (i + step > maxBytes)
				break;
			i += step;
		}
		return i;
	}

	// Widens a NUL-terminated name into destination (capacity in wchar_t,
	// NUL included). Invalid bytes for the code page fall back to the old
	// byte-wise widening, so a name is never lost.
	inline void widen(const char* source, wchar_t* destination, size_t capacity, unsigned codePage) noexcept
	{
		if (capacity == 0)
			return;
		const size_t sourceLength = strnlen(source, 256);
		wchar_t converted[257] = {};
		const int count = sourceLength == 0 ? 0 : MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
			source, static_cast<int>(sourceLength), converted, 256);
		size_t i = 0;
		if (count > 0 || sourceLength == 0)
		{
			for (; i + 1 < capacity && i < static_cast<size_t>(count); i++)
				destination[i] = converted[i];
		}
		else
		{
			for (; i + 1 < capacity && source[i] != '\0'; i++)
				destination[i] = static_cast<wchar_t>(static_cast<unsigned char>(source[i]));
		}
		destination[i] = L'\0';
	}
}
