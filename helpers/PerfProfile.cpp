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
#include <iomanip>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace PerfProfile
{

std::atomic<bool> g_active{false};

namespace
{
	struct Entry
	{
		uint64_t count = 0;
		double total = 0.0;
		double minVal = 0.0;
		double maxVal = 0.0;
	};

	std::mutex s_mutex;
	std::unordered_map<std::string, Entry> s_entries;
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
	std::lock_guard<std::mutex> lock(s_mutex);
	s_entries.clear();
}

void record(const char* label, double seconds)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	auto& entry = s_entries[label];
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
	std::vector<std::pair<std::string, Entry>> rows;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		rows.assign(s_entries.begin(), s_entries.end());
	}

	std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
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
		const auto& label = row.first;
		const auto& entry = row.second;
		double avg_us = entry.count > 0 ? (entry.total / entry.count) * 1e6 : 0.0;
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
