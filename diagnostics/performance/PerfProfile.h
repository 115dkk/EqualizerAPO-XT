/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <atomic>
#include <cstdint>
#include <iosfwd>

namespace PerfProfile
{
	// Active flag: relaxed atomic so the hot-path check is a single load with no fences.
	extern std::atomic<bool> g_active;

	inline bool active() noexcept
	{
		return g_active.load(std::memory_order_relaxed);
	}

	void enable();
	void disable();
	void reset();

	// Labels are keyed by address, kept in a fixed per-thread table and printed
	// long after the call. A label must therefore be a static-lifetime string with
	// one stable address per distinct text (a literal, or a type_info name), and
	// the set of distinct labels must stay small. Passing a temporary buffer would
	// both dangle and fill the table; report() prints how many samples the table
	// had to drop.
	void record(const char* label, double seconds);
	void report(std::ostream& os);
}

// RAII timer. Implementation lives in PerfProfile.cpp to avoid pulling
// <windows.h> (via PrecisionTimer.h) into every translation unit that
// only needs to declare a profile scope.
class PerfScope
{
	std::int64_t start_count_;
	const char* label_;
	bool active_;

public:
	explicit PerfScope(const char* label);
	~PerfScope();

	PerfScope(const PerfScope&) = delete;
	PerfScope& operator=(const PerfScope&) = delete;
};
