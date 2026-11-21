/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <cstdint>

#include "runtime/concurrency/FanOutGovernor.h"
#include "Tests/TestHarness.h"

namespace
{
	using eapo::rt::FanOutGovernor;
	constexpr uint64_t ticksPerSecond = 1000000000;
	// Six milliseconds of task work: heavy enough to wish for every worker.
	constexpr uint64_t heavyWorkTicks = 6000000;
	constexpr size_t manyTasks = 64;

	// Runs `windows` measurement windows of jobs whose duration depends only on
	// how many workers the governor woke.
	template<typename WallTicks>
	void run(FanOutGovernor& governor, unsigned teamSize, unsigned windows, WallTicks wallTicks)
	{
		for (unsigned job = 0; job < windows * FanOutGovernor::windowLength; ++job)
		{
			const unsigned workers = governor.plan(manyTasks, teamSize);
			governor.observe(heavyWorkTicks, wallTicks(workers), workers, ticksPerSecond);
		}
	}

	// A job that divides perfectly over the threads working on it, up to
	// `usefulThreads` of them.
	uint64_t scalingWall(unsigned workers, unsigned usefulThreads)
	{
		return heavyWorkTicks / (std::min)(workers + 1, usefulThreads);
	}
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorPlanCaps)
{
	FanOutGovernor governor;
	for (size_t tasks : {size_t{0}, size_t{1}, size_t{2}, size_t{8}, size_t{200}})
		for (unsigned teamSize : {0u, 1u, 3u, 15u, 63u})
		{
			const unsigned plan = governor.plan(tasks, teamSize);
			const size_t taskCap = tasks > 0 ? tasks - 1 : 0;
			harness.expectTrue(plan <= taskCap, "a plan never exceeds tasks minus one");
			harness.expectTrue(plan <= teamSize, "a plan never exceeds the available team");
		}
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorStartsAtReleasedCapacity)
{
	for (size_t tasks : {size_t{0}, size_t{1}, size_t{2}, size_t{7}, size_t{22}})
		for (unsigned teamSize : {0u, 1u, 6u, 15u})
		{
			FanOutGovernor governor;
			const unsigned expected = tasks < 2 ? 0u : static_cast<unsigned>((std::min)({
				tasks - 1, static_cast<size_t>(teamSize), size_t{7}}));
			harness.expectEqual(governor.plan(tasks, teamSize), expected,
				"the first plan matches the released seven-worker capacity");
		}
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorLightWorkStaysInline)
{
	FanOutGovernor governor;
	for (unsigned i = 0; i < 128; ++i)
		governor.observe(100, 100, 0, 1000000);
	harness.expectEqual(governor.plan(22, 15), 0u,
		"one hundred microseconds of total work does not wake a worker");
	harness.expectNear(governor.smoothedWorkSeconds(), 0.0001, 1e-12,
		"the light-work estimate converges to one hundred microseconds");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorFasterJobsEarnWorkers)
{
	FanOutGovernor governor;
	run(governor, 15, 60, [](unsigned workers) { return scalingWall(workers, 64); });
	harness.expectEqual(governor.settled(), 15u,
		"a job that keeps getting faster earns the whole team");
	harness.expectTrue(governor.ceiling() <= 15u, "a trial never asks for more than the team");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorBusyThreadsThatDoNotHelpAreGivenBack)
{
	// Six threads are all this job can use: the seventh and later ones are busy
	// (their task time is still six milliseconds in total) but the job ends no
	// sooner. The governor keeps five workers beside the caller.
	FanOutGovernor governor;
	run(governor, 15, 200, [](unsigned workers) { return scalingWall(workers, 6); });
	harness.expectEqual(governor.settled(), 5u,
		"workers that do not shorten the job are given back");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorSmallLossesDoNotAddUp)
{
	// More than seven workers gain nothing and each one fewer costs 0.9 percent.
	// Every single removal passes the one percent test; the third would put the
	// job 2.7 percent behind where the removals began.
	FanOutGovernor governor;
	run(governor, 15, 200, [](unsigned workers) {
		const unsigned missing = workers < 7 ? 7 - workers : 0;
		return heavyWorkTicks / 8 + heavyWorkTicks / 8 * 9 / 1000 * missing;
	});
	harness.expectEqual(governor.settled(), 5u,
		"removals stop two percent behind the count they started from");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorSlowerJobsLoseWorkers)
{
	FanOutGovernor governor;
	run(governor, 15, 200, [](unsigned workers) {
		return heavyWorkTicks + heavyWorkTicks / 10 * workers;
	});
	harness.expectEqual(governor.settled(), 1u,
		"a job every worker slows down keeps one worker and never fewer");
	harness.expectTrue(governor.plan(manyTasks, 15) >= 1u, "heavy work still wakes a worker");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorSmallTeamBoundsTheCount)
{
	FanOutGovernor governor;
	run(governor, 3, 60, [](unsigned workers) { return scalingWall(workers, 64); });
	harness.expectEqual(governor.settled(), 3u, "the count held never exceeds the team");
	harness.expectEqual(governor.ceiling(), 3u, "no trial asks for a worker the team lacks");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorSettledCountIsLeftAloneLonger)
{
	// Once both directions were refused the governor looks again less and less
	// often, so a settled count is disturbed by a trial in few windows.
	FanOutGovernor governor;
	run(governor, 15, 200, [](unsigned workers) { return scalingWall(workers, 6); });
	unsigned trialJobs = 0;
	const unsigned jobs = 64 * FanOutGovernor::windowLength;
	for (unsigned job = 0; job < jobs; ++job)
	{
		const unsigned workers = governor.plan(manyTasks, 15);
		if (workers != governor.settled())
			++trialJobs;
		governor.observe(heavyWorkTicks, scalingWall(workers, 6), workers, ticksPerSecond);
	}
	harness.expectTrue(trialJobs <= 4 * FanOutGovernor::windowLength,
		"a settled count is tried against its neighbours in at most four of sixty-four windows");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorInvalidClockLeavesStateUnchanged)
{
	FanOutGovernor governor;
	const unsigned planned = governor.plan(manyTasks, 15);
	for (unsigned i = 0; i < 4 * FanOutGovernor::windowLength; ++i)
	{
		governor.observe(6000, 0, planned, 1000000);
		governor.observe(6000, 1000, planned, 0);
	}
	harness.expectEqual(governor.ceiling(), 7u, "invalid clock data does not change the count");
	harness.expectEqual(governor.smoothedWorkSeconds(), 0.0,
		"invalid clock data does not change the work estimate");
}

TEST_CASE(EngineOrchestrationTests, testFanOutGovernorIgnoresJobsTheTeamDidNotServe)
{
	// The team was busy with another filter, so the job ran inline and took
	// long. That is not a measurement of the seven workers held.
	FanOutGovernor governor;
	for (unsigned job = 0; job < 100 * FanOutGovernor::windowLength; ++job)
	{
		governor.plan(manyTasks, 15);
		governor.observe(heavyWorkTicks, heavyWorkTicks, 0, ticksPerSecond);
	}
	harness.expectEqual(governor.settled(), 7u, "inline jobs do not move the count");
	harness.expectEqual(governor.ceiling(), 7u, "inline jobs do not start a trial");
}
