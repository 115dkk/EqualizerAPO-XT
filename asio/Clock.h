/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace eapo::asio::Clock
{
	inline int64_t ticks() noexcept
	{
		LARGE_INTEGER value;
		QueryPerformanceCounter(&value);
		return value.QuadPart;
	}

	inline const int64_t cachedFrequency = [] {
		LARGE_INTEGER value;
		QueryPerformanceFrequency(&value);
		return value.QuadPart;
	}();

	inline int64_t frequency() noexcept
	{
		return cachedFrequency;
	}

	inline double ticksPerMicrosecond() noexcept
	{
		return static_cast<double>(frequency()) / 1000000.0;
	}

	inline uint32_t microseconds(int64_t from, int64_t to) noexcept
	{
		const double divisor = ticksPerMicrosecond();
		if (to <= from || divisor <= 0.0)
			return 0;
		const double elapsed = static_cast<double>(to - from) / divisor;
		return elapsed >= 4294967295.0 ? 4294967295u : static_cast<uint32_t>(elapsed);
	}

	inline uint64_t nanoseconds() noexcept
	{
		const int64_t divisor = frequency();
		if (divisor <= 0)
			return 0;
		return static_cast<uint64_t>(static_cast<double>(ticks()) * 1000000000.0 / static_cast<double>(divisor));
	}
}
