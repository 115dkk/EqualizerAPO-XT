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

#include "stdafx.h"
#include "PerfProfile.h"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace PerfProfile
{

std::atomic<bool> g_active{false};

namespace
{
	struct Entry
	{
		std::uint64_t count = 0;
		double total = 0.0;
		double minVal = 0.0;
		double maxVal = 0.0;
	};

	// Key by const char*. PerfScope is always invoked with a string literal,
	// so identical labels share the same address and hash/eq can be reduced
	// to pointer comparison. If a separate TU happens to hold the same text
	// in a different buffer, only the entry is split; correctness is preserved.
	using EntryMap = std::unordered_map<const char*, Entry>;

	struct ThreadLocalStats;

	std::mutex& registry_mutex()
	{
		static std::mutex m;
		return m;
	}

	std::vector<ThreadLocalStats*>& registry()
	{
		static std::vector<ThreadLocalStats*> v;
		return v;
	}

	struct ThreadLocalStats
	{
		EntryMap entries;

		ThreadLocalStats()
		{
			std::lock_guard<std::mutex> lock(registry_mutex());
			registry().push_back(this);
		}

		~ThreadLocalStats()
		{
			std::lock_guard<std::mutex> lock(registry_mutex());
			auto& r = registry();
			r.erase(std::remove(r.begin(), r.end(), this), r.end());
		}
	};

	// Lock-free in the audio thread hot path. The thread takes the registry
	// lock once in the ctor and once in the dtor; for the entire audio stream
	// lifetime that is typically all the locking that happens.
	thread_local ThreadLocalStats tls_stats;

	LARGE_INTEGER qpc_frequency()
	{
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		return f;
	}
}

double qpc_to_seconds(std::int64_t delta)
{
	static LARGE_INTEGER freq = qpc_frequency();
	return static_cast<double>(delta) / static_cast<double>(freq.QuadPart);
}

void enable()
{
	g_active.store(true, std::memory_order_release);
}

void disable()
{
	g_active.store(false, std::memory_order_release);
}

void reset()
{
	std::lock_guard<std::mutex> lock(registry_mutex());
	for (auto* tls : registry())
		tls->entries.clear();
}

void record(const char* label, double seconds)
{
	Entry& entry = tls_stats.entries[label];
	if (entry.count == 0)
	{
		entry.minVal = seconds;
		entry.maxVal = seconds;
	}
	else
	{
		if (seconds < entry.minVal) entry.minVal = seconds;
		if (seconds > entry.maxVal) entry.maxVal = seconds;
	}
	entry.total += seconds;
	entry.count++;
}

void report(std::ostream& os)
{
	// Guard reading per-thread accumulators with the registry mutex. Reporting
	// runs outside the hot path so the lock cost is irrelevant here.
	std::unordered_map<const char*, Entry> merged;
	{
		std::lock_guard<std::mutex> lock(registry_mutex());
		for (const auto* tls : registry())
		{
			for (const auto& kv : tls->entries)
			{
				auto& m = merged[kv.first];
				if (m.count == 0)
				{
					m = kv.second;
				}
				else
				{
					m.count += kv.second.count;
					m.total += kv.second.total;
					if (kv.second.minVal < m.minVal) m.minVal = kv.second.minVal;
					if (kv.second.maxVal > m.maxVal) m.maxVal = kv.second.maxVal;
				}
			}
		}
	}

	std::vector<std::pair<std::string, Entry>> rows;
	rows.reserve(merged.size());
	for (const auto& kv : merged)
		rows.emplace_back(std::string(kv.first), kv.second);

	std::sort(rows.begin(), rows.end(), [](const std::pair<std::string, Entry>& a, const std::pair<std::string, Entry>& b) {
		return a.second.total > b.second.total;
	});

	os << "\n=== PerfProfile (sorted by total time) ===\n";
	os << "  " << std::left << std::setw(56) << "Label"
		<< std::right << std::setw(10) << "Calls"
		<< std::setw(14) << "Total(ms)"
		<< std::setw(12) << "Avg(us)"
		<< std::setw(12) << "Min(us)"
		<< std::setw(12) << "Max(us)"
		<< "\n";
	os << "  " << std::string(56 + 10 + 14 + 12 + 12 + 12, '-') << "\n";

	for (const auto& row : rows)
	{
		const std::string& label = row.first;
		const Entry& entry = row.second;
		double avg_us = entry.count > 0 ? (entry.total / static_cast<double>(entry.count)) * 1e6 : 0.0;
		os << "  " << std::left << std::setw(56) << label
			<< std::right << std::setw(10) << entry.count
			<< std::setw(14) << std::fixed << std::setprecision(3) << (entry.total * 1000.0)
			<< std::setw(12) << std::fixed << std::setprecision(2) << avg_us
			<< std::setw(12) << std::fixed << std::setprecision(2) << (entry.minVal * 1e6)
			<< std::setw(12) << std::fixed << std::setprecision(2) << (entry.maxVal * 1e6)
			<< "\n";
	}
	os << "=================================================\n";
}

}

PerfScope::PerfScope(const char* label)
	: start_count_(0), label_(label), active_(PerfProfile::active())
{
	if (active_)
	{
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		start_count_ = c.QuadPart;
	}
}

PerfScope::~PerfScope()
{
	if (active_)
	{
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		PerfProfile::record(label_, PerfProfile::qpc_to_seconds(c.QuadPart - start_count_));
	}
}
