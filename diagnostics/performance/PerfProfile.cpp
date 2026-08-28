/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

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
#include "diagnostics/performance/PerfProfile.h"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
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

	// Key by const char*. PerfScope is always invoked with a string literal or a
	// type_info name, so one label means one address and lookup reduces to pointer
	// comparison. If a separate TU happens to hold the same text in a different
	// buffer, only the entry is split; correctness is preserved.
	struct Slot
	{
		const char* label = nullptr;
		Entry entry;
	};

	// Fixed slot count. The label set is bounded by the PerfScope call sites: four
	// I/O traits with four labels each, three configuration passes, one label per
	// filter class. That is well under half of this table, and a fixed table is
	// what keeps record() free of allocation on the audio thread. Power of two so
	// the probe index is a mask.
	constexpr size_t kSlotCount = 128;
	constexpr size_t kSlotMask = kSlotCount - 1;

	// Registered threads. Fixed size for the same reason as the slot table: a
	// thread that starts recording inside an audio callback must not allocate to
	// join. A thread arriving after the table is full still records locally; only
	// its rows are missing from report(), which is why the count is generous.
	constexpr size_t kMaxThreads = 64;

	// Fibonacci hash of the label address. Literals are packed densely in .rdata,
	// so the low bits alone would collide; the product carries the entropy up.
	size_t slotIndex(const char* label) noexcept
	{
		const std::uint64_t mixed = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(label)) * 0x9E3779B97F4A7C15ull;
		return static_cast<size_t>(mixed >> 32) & kSlotMask;
	}

	struct ThreadLocalStats;

	std::mutex& registry_mutex()
	{
		static std::mutex m;
		return m;
	}

	ThreadLocalStats** registry()
	{
		// Constant-initialized, so no guard variable and no allocation on first use.
		static ThreadLocalStats* threads[kMaxThreads] = {};
		return threads;
	}

	struct ThreadLocalStats
	{
		// Not named "slots": Qt defines that as an empty macro, and this file is
		// compiled into the Editor behind a precompiled header that includes Qt,
		// so the member would preprocess away there while building fine anywhere
		// else. Same reason to avoid signals, emit, foreach and forever.
		Slot slotTable[kSlotCount];
		// Samples dropped because every probe found a foreign label. Reported, so a
		// truncated profile is never silently wrong.
		std::uint64_t dropped = 0;

		ThreadLocalStats()
		{
			std::lock_guard<std::mutex> lock(registry_mutex());
			ThreadLocalStats** threads = registry();
			for (size_t i = 0; i < kMaxThreads; i++)
			{
				if (threads[i] == nullptr)
				{
					threads[i] = this;
					return;
				}
			}
		}

		~ThreadLocalStats()
		{
			std::lock_guard<std::mutex> lock(registry_mutex());
			ThreadLocalStats** threads = registry();
			for (size_t i = 0; i < kMaxThreads; i++)
			{
				if (threads[i] == this)
				{
					threads[i] = nullptr;
					return;
				}
			}
		}
	};

	// Lock-free and allocation-free in the audio thread hot path. The thread takes
	// the registry lock once in the ctor and once in the dtor; for the entire audio
	// stream lifetime that is typically all the locking that happens.
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
	// Meant to run while nothing is recording (Benchmark calls it before enable()).
	// Clearing a slot another thread is writing can only mix two samples of one
	// label now that the table is plain storage; it cannot corrupt a container.
	std::lock_guard<std::mutex> lock(registry_mutex());
	ThreadLocalStats** threads = registry();
	for (size_t i = 0; i < kMaxThreads; i++)
	{
		ThreadLocalStats* tls = threads[i];
		if (tls == nullptr)
			continue;
		for (size_t s = 0; s < kSlotCount; s++)
			tls->slotTable[s] = Slot();
		tls->dropped = 0;
	}
}

void record(const char* label, double seconds)
{
	// Open addressing over a table that is allocated once per thread: claiming a
	// label writes into storage that already exists, so the first measurement of a
	// label costs the same as every later one. That matters because enabling the
	// profiler would otherwise charge every label's first sample with a malloc.
	ThreadLocalStats& stats = tls_stats;
	size_t index = slotIndex(label);
	for (size_t probe = 0; probe < kSlotCount; probe++)
	{
		Slot& slot = stats.slotTable[index];
		if (slot.label == nullptr || slot.label == label)
		{
			if (slot.entry.count == 0)
			{
				// Also the path a slot takes after reset() cleared it.
				slot.label = label;
				slot.entry.minVal = seconds;
				slot.entry.maxVal = seconds;
			}
			else
			{
				if (seconds < slot.entry.minVal) slot.entry.minVal = seconds;
				if (seconds > slot.entry.maxVal) slot.entry.maxVal = seconds;
			}
			slot.entry.total += seconds;
			slot.entry.count++;
			return;
		}
		index = (index + 1) & kSlotMask;
	}

	stats.dropped++;
}

void report(std::ostream& os)
{
	// Guard reading per-thread accumulators with the registry mutex. Reporting
	// runs outside the hot path so the lock cost is irrelevant here, and merging
	// a few dozen rows by linear search needs no container of its own.
	struct Row
	{
		const char* label = nullptr;
		Entry entry;
	};

	std::vector<Row> rows;
	std::uint64_t dropped = 0;
	{
		std::lock_guard<std::mutex> lock(registry_mutex());
		ThreadLocalStats** threads = registry();
		for (size_t i = 0; i < kMaxThreads; i++)
		{
			const ThreadLocalStats* tls = threads[i];
			if (tls == nullptr)
				continue;
			dropped += tls->dropped;
			for (size_t s = 0; s < kSlotCount; s++)
			{
				const Slot& slot = tls->slotTable[s];
				if (slot.entry.count == 0)
					continue;

				Row* row = nullptr;
				for (Row& candidate : rows)
				{
					if (candidate.label == slot.label)
					{
						row = &candidate;
						break;
					}
				}
				if (row == nullptr)
				{
					rows.push_back(Row{slot.label, slot.entry});
					continue;
				}
				row->entry.count += slot.entry.count;
				row->entry.total += slot.entry.total;
				if (slot.entry.minVal < row->entry.minVal) row->entry.minVal = slot.entry.minVal;
				if (slot.entry.maxVal > row->entry.maxVal) row->entry.maxVal = slot.entry.maxVal;
			}
		}
	}

	// Labels outlive the threads that recorded them (string literals, type_info
	// names), so holding the pointers past the lock is safe.
	std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
		return a.entry.total > b.entry.total;
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
		const char* label = row.label;
		const Entry& entry = row.entry;
		double avg_us = entry.count > 0 ? (entry.total / static_cast<double>(entry.count)) * 1e6 : 0.0;
		os << "  " << std::left << std::setw(56) << label
			<< std::right << std::setw(10) << entry.count
			<< std::setw(14) << std::fixed << std::setprecision(3) << (entry.total * 1000.0)
			<< std::setw(12) << std::fixed << std::setprecision(2) << avg_us
			<< std::setw(12) << std::fixed << std::setprecision(2) << (entry.minVal * 1e6)
			<< std::setw(12) << std::fixed << std::setprecision(2) << (entry.maxVal * 1e6)
			<< "\n";
	}
	if (dropped != 0)
		os << "  (" << dropped << " sample(s) dropped: more than " << kSlotCount << " distinct labels)\n";
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
