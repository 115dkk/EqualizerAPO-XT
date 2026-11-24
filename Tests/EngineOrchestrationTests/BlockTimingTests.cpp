/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cstdint>

#include "diagnostics/performance/BlockTiming.h"
#include "Tests/TestHarness.h"

namespace
{
	using eapo::diag::BlockTimingMonitor;
	using eapo::diag::BlockTimingSummary;

	constexpr int64_t ticksPerSecond = 10'000'000;
	constexpr unsigned frames = 480;
	constexpr double sampleRate = 48000.0;
	constexpr uint64_t budgetTicks = 100000;

	void expectZero(test::Harness& harness, const BlockTimingSummary& summary)
	{
		harness.expectEqual(summary.blocks, uint64_t{0}, "zero summary has no blocks");
		harness.expectEqual(summary.overBudget, uint64_t{0}, "zero summary has no over-budget blocks");
		harness.expectEqual(summary.lateStarts, uint64_t{0}, "zero summary has no late starts");
		harness.expectEqual(summary.longestTicks, uint64_t{0}, "zero summary has no longest block");
		harness.expectEqual(summary.longestBudgetTicks, uint64_t{0}, "zero summary has no longest-block budget");
		harness.expectEqual(summary.longestGapTicks, uint64_t{0}, "zero summary has no longest gap");
		harness.expectEqual(summary.lastBudgetTicks, uint64_t{0}, "zero summary has no last budget");
	}
}

TEST_CASE(EngineOrchestrationTests, testBlockTimingBudgetAndTake)
{
	BlockTimingMonitor monitor;
	monitor.observe(1, 100001, frames, sampleRate, ticksPerSecond);
	monitor.observe(100001, 200002, frames, sampleRate, ticksPerSecond);

	const BlockTimingSummary summary = monitor.take();
	harness.expectEqual(summary.blocks, uint64_t{2}, "both valid callbacks are counted");
	harness.expectEqual(summary.overBudget, uint64_t{1}, "exact budget is on time and budget plus one is over");
	harness.expectEqual(summary.longestTicks, budgetTicks + 1, "the longest callback duration is retained");
	harness.expectEqual(summary.longestBudgetTicks, budgetTicks, "the longest callback retains its audio budget");
	expectZero(harness, monitor.take());
}

TEST_CASE(EngineOrchestrationTests, testBlockTimingGapThresholdAndRestart)
{
	BlockTimingMonitor monitor;
	monitor.observe(1, 2, frames, sampleRate, ticksPerSecond);
	BlockTimingSummary summary = monitor.take();
	harness.expectEqual(summary.lateStarts, uint64_t{0}, "the first callback has no late start");
	harness.expectEqual(summary.longestGapTicks, uint64_t{0}, "the first callback has no gap");

	monitor.streamRestarted();
	monitor.observe(20'000'000, 20'000'001, frames, sampleRate, ticksPerSecond);
	summary = monitor.take();
	harness.expectEqual(summary.lateStarts, uint64_t{0}, "the first callback after restart has no late start");
	harness.expectEqual(summary.longestGapTicks, uint64_t{0}, "the first callback after restart has no gap");

	monitor.observe(20'150'000, 20'150'001, frames, sampleRate, ticksPerSecond);
	monitor.observe(20'300'001, 20'300'002, frames, sampleRate, ticksPerSecond);
	monitor.observe(30'300'002, 30'300'003, frames, sampleRate, ticksPerSecond);
	summary = monitor.take();
	harness.expectEqual(summary.lateStarts, uint64_t{1}, "150000 ticks is on time and 150001 is late");
	harness.expectEqual(summary.longestGapTicks, uint64_t{150001}, "an idle gap over one second is not the longest gap");
}

TEST_CASE(EngineOrchestrationTests, testBlockTimingInvalidInputsLeaveStateUnchanged)
{
	BlockTimingMonitor monitor;
	monitor.observe(1, 2, frames, sampleRate, ticksPerSecond);
	monitor.take();

	monitor.observe(1'000'000, 1'000'001, 0, sampleRate, ticksPerSecond);
	monitor.observe(2'000'000, 2'000'001, frames, 0.0, ticksPerSecond);
	monitor.observe(3'000'000, 2'999'999, frames, sampleRate, ticksPerSecond);
	monitor.observe(4'000'000, 4'000'001, frames, sampleRate, 0);
	monitor.observe(100001, 100002, frames, sampleRate, ticksPerSecond);

	const BlockTimingSummary summary = monitor.take();
	harness.expectEqual(summary.blocks, uint64_t{1}, "invalid callbacks do not change the block count");
	harness.expectEqual(summary.overBudget, uint64_t{0}, "invalid callbacks do not change the over-budget count");
	harness.expectEqual(summary.lateStarts, uint64_t{0}, "invalid callbacks do not create a late start");
	harness.expectEqual(summary.longestGapTicks, budgetTicks, "invalid callbacks do not replace the previous start");
}

TEST_CASE(EngineOrchestrationTests, testBlockTimingLastBudgetFollowsMostRecentCall)
{
	BlockTimingMonitor monitor;
	monitor.observe(1, 2, frames, sampleRate, ticksPerSecond);
	monitor.observe(100001, 100002, frames * 2, sampleRate, ticksPerSecond);

	const BlockTimingSummary summary = monitor.take();
	harness.expectEqual(summary.lastBudgetTicks, uint64_t{200000}, "the last budget follows the 960-frame callback");
}
