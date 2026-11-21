/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <thread>

#include "dsp/MxcsrGuard.h"
#include "runtime/concurrency/FanOutGovernor.h"
#include "runtime/concurrency/RealtimeFanOut.h"
#include "EngineOrchestrationTestSupport.h"
#include "services/registry/RegistryPaths.h"
#include "Tests/FakeRegistry.h"
#include "Tests/WavFixtures.h"

#ifndef __SANITIZE_ADDRESS__
namespace allocation_gate
{
	void begin() noexcept;
	size_t end() noexcept;
}
#endif

namespace
{
	using eapo::rt::FanOut;
	using eapo::rt::FanOutGovernor;
	constexpr unsigned maximumWorkerLimit = 63;
	struct Counts
	{
		std::array<std::atomic<unsigned>, 256> hits{};
		std::atomic<unsigned> wrongThread{0};
		DWORD caller = 0;
	};

	void countTask(void* context, size_t index) noexcept
	{
		auto& counts = *static_cast<Counts*>(context);
		counts.hits[index].fetch_add(1, std::memory_order_relaxed);
		if (counts.caller != 0 && counts.caller != GetCurrentThreadId())
			counts.wrongThread.fetch_add(1, std::memory_order_relaxed);
	}

	void checkCounts(test::Harness& harness, const Counts& counts, size_t count)
	{
		for (size_t i = 0; i < counts.hits.size(); ++i)
			harness.expectEqual(counts.hits[i].load(), i < count ? 1u : 0u,
				"each index completes exactly once before run returns");
	}
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutEpochs)
{
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
	auto lease = FanOut::Lease::acquire();
	harness.require(FanOut::workerCount() > 0, "a worker team is running for the epoch stress test");
	constexpr size_t sizes[] = {0, 1, 2, 7, 64};
	for (unsigned job = 0; job < 20000; ++job)
	{
		// Destroy the context immediately after return. ASan catches a worker
		// retaining an old job, including when the next job reuses its address.
		auto counts = std::make_unique<Counts>();
		const size_t count = sizes[job % std::size(sizes)];
		FanOut::run(count, countTask, counts.get());
		checkCounts(harness, *counts, count);
	}
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutUsesLogicalProcessorCapacity)
{
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
	if (std::thread::hardware_concurrency() <= 8)
	{
		harness.skip("the machine has no more than eight logical processors");
		return;
	}
	{
		auto lease = FanOut::Lease::acquire();
		harness.expectTrue(FanOut::workerCount() > 7,
			"a machine with more than eight logical processors starts more than seven workers");
	}
	harness.expectEqual(FanOut::workerCount(), 0u, "the capacity test releases its worker team");
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutGovernedRunsCompleteExactlyOnce)
{
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
	auto lease = FanOut::Lease::acquire();
	harness.require(FanOut::workerCount() > 0, "a worker team is running for governed jobs");
	constexpr size_t sizes[] = {0, 1, 2, 200};

	FanOutGovernor inlineGovernor;
	inlineGovernor.observe(100, 100, 0, 1000000);
	for (const size_t count : sizes)
	{
		Counts counts;
		FanOut::run(count, countTask, &counts, inlineGovernor);
		checkCounts(harness, counts, count);
		harness.expectEqual(FanOut::lastWorkersWoken(), 0u,
			"a light governed job stays on the caller");
	}

	for (const size_t count : sizes)
	{
		FanOutGovernor parallelGovernor;
		Counts counts;
		FanOut::run(count, countTask, &counts, parallelGovernor);
		checkCounts(harness, counts, count);
		const unsigned expected = count < 2 ? 0u : static_cast<unsigned>((std::min)({
			count - 1, static_cast<size_t>(FanOut::workerCount()), size_t{7}}));
		harness.expectEqual(FanOut::lastWorkersWoken(), expected,
			"an unmeasured governed job wakes the released worker count");
	}
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutGovernedRunReportsTiming)
{
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
	auto lease = FanOut::Lease::acquire();
	FanOutGovernor governor;
	Counts counts;
	FanOut::run(200, countTask, &counts, governor);
	checkCounts(harness, counts, 200);
	harness.expectTrue(governor.smoothedWorkSeconds() > 0.0,
		"a governed run reports positive task work to its governor");
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutZeroLimitForcesGovernedInline)
{
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
	auto lease = FanOut::Lease::acquire();
	FanOut::setWorkerLimitForTests(0);
	FanOutGovernor governor;
	Counts counts;
	counts.caller = GetCurrentThreadId();
	FanOut::run(200, countTask, &counts, governor);
	checkCounts(harness, counts, 200);
	harness.expectEqual(counts.wrongThread.load(), 0u,
		"a zero test limit keeps a governed run on its caller");
	harness.expectEqual(FanOut::lastWorkersWoken(), 0u,
		"a zero test limit reports that no workers were woken");
	harness.expectTrue(governor.smoothedWorkSeconds() > 0.0,
		"a forced-inline governed run still reports its task work");
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutBusyCallerRunsInline)
{
	auto lease = FanOut::Lease::acquire();
	struct HeldJob
	{
		Counts counts;
		std::atomic<bool> entered{false};
		std::atomic<bool> release{false};
	} held;
	std::thread first([&] {
		FanOut::run(64, [](void* context, size_t index) noexcept {
			auto& job = *static_cast<HeldJob*>(context);
			countTask(&job.counts, index);
			if (index == 0)
			{
				job.entered.store(true);
				while (!job.release.load())
					Sleep(1);
			}
		}, &held);
	});
	while (!held.entered.load())
		Sleep(1);
	Counts second;
	second.caller = GetCurrentThreadId();
	FanOut::run(64, countTask, &second);
	held.release.store(true);
	first.join();
	checkCounts(harness, held.counts, 64);
	checkCounts(harness, second, 64);
	harness.expectEqual(second.wrongThread.load(), 0u, "a busy slot never makes another endpoint wait");
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutLeaseLifecycle)
{
	for (unsigned cycle = 0; cycle < 200; ++cycle)
	{
		{
			auto lease = FanOut::Lease::acquire();
			auto other = FanOut::Lease::acquire();
			// Move assignment over a held lease is the behaviour under test:
			// it must release the first lease, not leak or double-count it.
			// cppcheck-suppress redundantInitialization
			lease = std::move(other);
			Counts counts;
			FanOut::run(64, countTask, &counts);
			checkCounts(harness, counts, 64);
		}
		harness.expectEqual(FanOut::workerCount(), 0u, "the last lease joins every worker");
		Counts counts;
		counts.caller = GetCurrentThreadId();
		FanOut::run(64, countTask, &counts);
		checkCounts(harness, counts, 64);
		harness.expectEqual(counts.wrongThread.load(), 0u, "without a lease run stays inline");
	}
	FanOut::setWorkerLimitForTests(0);
	{
		auto lease = FanOut::Lease::acquire();
		harness.expectEqual(FanOut::workerCount(), 0u, "zero limit does not create a team");
	}
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutFpControlAndLimit)
{
	auto lease = FanOut::Lease::acquire();
	MxcsrFtzDazGuard guard;
	struct FpJob
	{
		unsigned __int64 expected;
		std::atomic<unsigned> mismatches{0};
	} fp{captureFpControlState()};
	FanOut::run(64, [](void* context, size_t) noexcept {
		auto& job = *static_cast<FpJob*>(context);
		if (captureFpControlState() != job.expected)
			job.mismatches.fetch_add(1);
	}, &fp);
	harness.expectEqual(fp.mismatches.load(), 0u, "workers inherit the caller's FP state");
	FanOut::setWorkerLimitForTests(0);
	Counts counts;
	counts.caller = GetCurrentThreadId();
	FanOut::run(64, countTask, &counts);
	checkCounts(harness, counts, 64);
	harness.expectEqual(counts.wrongThread.load(), 0u, "zero limit also bypasses an existing team");
	FanOut::setWorkerLimitForTests(maximumWorkerLimit);
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutWaitsForAdmittedWorker)
{
	auto lease = FanOut::Lease::acquire();
	struct HeldWorker
	{
		Counts counts;
		std::atomic<bool> entered{false};
		std::atomic<bool> release{false};
		std::atomic<bool> returned{false};
		DWORD caller = 0;
	} held;
	std::thread caller([&] {
		held.caller = GetCurrentThreadId();
		FanOut::run(64, [](void* context, size_t index) noexcept {
			auto& job = *static_cast<HeldWorker*>(context);
			if (GetCurrentThreadId() != job.caller)
			{
				job.entered.store(true);
				while (!job.release.load())
					Sleep(1);
			}
			else
			{
				// Give the sleeping team time to enter, rather than accidentally
				// exercising an entirely inline job on a slow CI machine.
				const ULONGLONG deadline = GetTickCount64() + 2000;
				while (!job.entered.load() && GetTickCount64() < deadline)
					Sleep(1);
			}
			countTask(&job.counts, index);
		}, &held);
		held.returned.store(true);
	});
	const ULONGLONG deadline = GetTickCount64() + 5000;
	while (!held.entered.load() && !held.returned.load() && GetTickCount64() < deadline)
		Sleep(1);
	const bool entered = held.entered.load();
	Sleep(10);
	const bool returnedEarly = held.returned.load();
	held.release.store(true);
	caller.join();
	harness.expectTrue(entered, "a real worker entered the job");
	harness.expectFalse(returnedEarly, "run waits for the admitted worker before releasing the context");
	checkCounts(harness, held.counts, 64);
}

TEST_CASE(EngineOrchestrationTests, testParallelConvolutionSettingReachesBothFactories)
{
	const auto path = testDirectory() + L"\\parallel-setting-ir.wav";
	trackWrittenFile(path);
	harness.require(test::writeWavFile(path, 48000, std::vector<double>(96000, 0.001)), "setting test IR is written");
	for (const std::string command : {"Convolution: ", "MultiConvolution: L=0 R=0 "})
	{
		const auto config = writeConfig(harness, L"parallel-setting.txt", command + "\"" + toNarrow(path) + "\"\n");
		for (const bool enabled : {false, true})
		{
			test::FakeRegistry registry;
			registry.seedString(APP_REGPATH, parallelConvolutionValueName, enabled ? L"true" : L"false");
			{
				FilterEngine engine;
				// Long enough at this block size to pass useConvolverFanOut: a filter
				// below the threshold starts no workers whatever the setting says.
				auto setup = testEngineSetup(48000, 2, 2, 480, config);
				setup.registry = &registry;
				engine.initialize(setup);
				harness.expectEqual(FanOut::workerCount() != 0, enabled, "the factory passes the injected setting to its filter");
			}
			harness.expectEqual(FanOut::workerCount(), 0u, "engine cleanup releases the filter team");
		}
	}
}

TEST_CASE(EngineOrchestrationTests, testRealtimeFanOutDoesNotAllocate)
{
#ifdef __SANITIZE_ADDRESS__
	std::printf("FanOut allocation contract: skipped under AddressSanitizer allocator interposition\n");
#else
	auto lease = FanOut::Lease::acquire();
	Counts counts;
	FanOutGovernor governor;
	allocation_gate::begin();
	for (unsigned i = 0; i < 100; ++i)
	{
		FanOut::run(64, countTask, &counts);
		FanOut::run(64, countTask, &counts, governor);
	}
	const size_t allocations = allocation_gate::end();
	harness.expectEqual(allocations, size_t{0},
		"plain and governed runs never allocate on the calling thread, including their first calls");
#endif
}
