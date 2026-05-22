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
#include <ostream>
#include "PrecisionTimer.h"

namespace PerfProfile
{
	// Active flag: relaxed atomic so the hot-path check is a single load with no fences.
	// The release/uninstall build of EqualizerAPO.dll links the same translation units,
	// so the flag stays off by default and Benchmark.exe is the only consumer that flips it.
	extern std::atomic<bool> g_active;

	inline bool active() noexcept
	{
		return g_active.load(std::memory_order_relaxed);
	}

	void enable();
	void disable();
	void reset();
	void record(const char* label, double seconds);
	void report(std::ostream& os);
}

class PerfScope
{
	PrecisionTimer timer_;
	const char* label_;
	bool active_;

public:
	explicit PerfScope(const char* label) noexcept
		: label_(label), active_(PerfProfile::active())
	{
		if (active_)
			timer_.start();
	}

	~PerfScope() noexcept
	{
		if (active_)
			PerfProfile::record(label_, timer_.stop());
	}

	PerfScope(const PerfScope&) = delete;
	PerfScope& operator=(const PerfScope&) = delete;
};
