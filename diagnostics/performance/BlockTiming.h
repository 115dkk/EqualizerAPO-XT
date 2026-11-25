/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <atomic>
#include <cstdint>

#ifdef _MSC_VER
#pragma code_seg(push, "RT_CODE")
#endif

namespace eapo::diag
{
	struct BlockTimingSummary
	{
		uint64_t blocks = 0;          // calls observed
		uint64_t overBudget = 0;      // calls that took longer than the audio they carried
		uint64_t lateStarts = 0;      // calls that began more than 1.5 block lengths after the previous one
		uint64_t longestTicks = 0;    // longest call
		uint64_t longestBudgetTicks = 0; // the audio duration of that longest call
		uint64_t longestGapTicks = 0; // longest counted gap between successive call starts
		uint64_t lastBudgetTicks = 0; // audio duration of the most recent call
	};

	class BlockTimingMonitor
	{
	public:
		// Audio thread. One call per host callback: QPC ticks at entry and at exit,
		// the frames the callback carried, the stream's sample rate, QPC ticks per second.
		void observe(int64_t entryTicks, int64_t exitTicks, unsigned frames,
			double sampleRate, int64_t ticksPerSecond) noexcept
		{
			if (frames == 0 || sampleRate <= 0 || ticksPerSecond <= 0 || exitTicks < entryTicks)
				return;

			const uint64_t budgetTicks = static_cast<uint64_t>(
				static_cast<double>(frames) * static_cast<double>(ticksPerSecond) / sampleRate);
			const uint64_t blockTicks = static_cast<uint64_t>(exitTicks - entryTicks);
			blocks.fetch_add(1, std::memory_order_relaxed);
			if (blockTicks > budgetTicks)
				overBudget.fetch_add(1, std::memory_order_relaxed);
			const uint64_t observedLongest = longestTicks.load(std::memory_order_relaxed);
			if (blockTicks > observedLongest)
			{
				longestTicks.store(blockTicks, std::memory_order_relaxed);
				longestBudgetTicks.store(budgetTicks, std::memory_order_relaxed);
			}
			lastBudgetTicks.store(budgetTicks, std::memory_order_relaxed);

			if (previousEntry != 0)
			{
				const int64_t gap = entryTicks - previousEntry;
				if (gap > 0 && gap <= ticksPerSecond)
				{
					const uint64_t gapTicks = static_cast<uint64_t>(gap);
					if (gapTicks > previousBudget + previousBudget / 2)
						lateStarts.fetch_add(1, std::memory_order_relaxed);
					if (gapTicks > longestGapTicks.load(std::memory_order_relaxed))
						longestGapTicks.store(gapTicks, std::memory_order_relaxed);
				}
			}
			previousEntry = entryTicks;
			previousBudget = budgetTicks;
		}

		// Before a stream starts (LockForProcess): forget the previous call's start,
		// so the first gap of a new stream is not counted. Not concurrent with observe().
		void streamRestarted() noexcept
		{
			previousEntry = 0;
			previousBudget = 0;
		}

		// Any thread: everything observed since the last take(), and zero the counters.
		BlockTimingSummary take() noexcept
		{
			// A concurrent observe() may split one call's fields across two summaries.
			BlockTimingSummary summary;
			summary.blocks = blocks.exchange(0, std::memory_order_relaxed);
			summary.overBudget = overBudget.exchange(0, std::memory_order_relaxed);
			summary.lateStarts = lateStarts.exchange(0, std::memory_order_relaxed);
			summary.longestTicks = longestTicks.exchange(0, std::memory_order_relaxed);
			summary.longestBudgetTicks = longestBudgetTicks.exchange(0, std::memory_order_relaxed);
			summary.longestGapTicks = longestGapTicks.exchange(0, std::memory_order_relaxed);
			summary.lastBudgetTicks = lastBudgetTicks.exchange(0, std::memory_order_relaxed);
			return summary;
		}

	private:
		std::atomic<uint64_t> blocks{0};
		std::atomic<uint64_t> overBudget{0};
		std::atomic<uint64_t> lateStarts{0};
		std::atomic<uint64_t> longestTicks{0};
		std::atomic<uint64_t> longestBudgetTicks{0};
		std::atomic<uint64_t> longestGapTicks{0};
		std::atomic<uint64_t> lastBudgetTicks{0};
		int64_t previousEntry = 0;
		uint64_t previousBudget = 0;
	};
}

#ifdef _MSC_VER
#pragma code_seg(pop)
#endif
