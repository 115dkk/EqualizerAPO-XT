/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "RealtimeFanOut.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <avrt.h>

#include "dsp/MxcsrGuard.h"
#include "runtime/memory/AlignedMemory.h"

namespace eapo::rt
{
	namespace
	{
		constexpr unsigned maximumWorkers = 7;
		constexpr uint64_t insideMask = 0xff;
		constexpr uint64_t openBit = 0x100;
		constexpr uint64_t epochStep = 0x200;
		static_assert(std::atomic<uint64_t>::is_always_lock_free);
		static_assert(std::atomic<size_t>::is_always_lock_free);

		struct Team
		{
			std::array<HANDLE, maximumWorkers> wake{};
			std::array<std::thread, maximumWorkers> workers;
			HANDLE done = nullptr;
			std::atomic<bool> stopping{false};
			// One CAS word contains epoch, admission, and inside-job references.
			// Never reset the epoch between jobs (55 bits before wraparound).
			std::atomic<uint64_t> admission{0};
			std::atomic<size_t> next{0};
			size_t count = 0;
			FanOut::Task fn = nullptr;
			void* context = nullptr;
			unsigned __int64 fpState = 0;
			unsigned activeWorkers = 0;
			unsigned size = 0;

			void start(unsigned count);
			void serve(unsigned index) noexcept;
			void work() noexcept;
			~Team();
		};

		// These have no team-owning destructor. Only Lease::acquire/release
		// touches the heap or lifecycle mutex. run() accesses team only while
		// holding slot, including when racing the last lease's teardown.
		std::atomic_flag slot = ATOMIC_FLAG_INIT;
		std::atomic<unsigned> running{0};
		std::atomic<unsigned> workerLimit{maximumWorkers};
		std::mutex lifetimeMutex;
		Team* team = nullptr;
		unsigned leases = 0;

		void takeSlotForLifecycle() noexcept
		{
			while (slot.test_and_set(std::memory_order_acquire))
				Sleep(1);
		}

		void releaseLease() noexcept
		{
			std::lock_guard<std::mutex> lock(lifetimeMutex);
			if (--leases != 0)
				return;
			takeSlotForLifecycle();
			running.store(0, std::memory_order_release);
			delete team;
			team = nullptr;
			slot.clear(std::memory_order_release);
		}

		void Team::start(unsigned count)
		{
			done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (done == nullptr)
				throw std::runtime_error("FanOut completion event creation failed");
			for (unsigned i = 0; i < count; ++i)
			{
				wake[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				if (wake[i] == nullptr)
					throw std::runtime_error("FanOut worker event creation failed");
				workers[i] = std::thread([this, i] { serve(i); });
				++size;
			}
		}

		Team::~Team()
		{
			stopping.store(true, std::memory_order_release);
			for (HANDLE event : wake)
				if (event != nullptr)
					SetEvent(event);
			for (auto& worker : workers)
				if (worker.joinable())
					worker.join();
			for (HANDLE event : wake)
				if (event != nullptr)
					CloseHandle(event);
			if (done != nullptr)
				CloseHandle(done);
		}

#pragma AVRT_CODE_BEGIN
		void Team::work() noexcept
		{
			for (;;)
			{
				const size_t index = next.fetch_add(1, std::memory_order_relaxed);
				if (index >= count)
					return;
				fn(context, index);
			}
		}
#pragma AVRT_CODE_END

		void Team::serve(unsigned index) noexcept
		{
			DWORD taskIndex = 0;
			HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
			if (mmcss != nullptr)
				AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_CRITICAL);
			else
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

			for (;;)
			{
				WaitForSingleObject(wake[index], INFINITE);
				if (stopping.load(std::memory_order_acquire))
					break;
				uint64_t observed = admission.load(std::memory_order_acquire);
				const uint64_t epoch = observed & ~(insideMask | openBit);
				while ((observed & openBit) != 0)
				{
					// A late wake may join the currently published job, but an
					// already observed old epoch must never enter a new one.
					if ((observed & ~(insideMask | openBit)) != epoch)
						break;
					if (!admission.compare_exchange_weak(observed, observed + 1,
						std::memory_order_acq_rel, std::memory_order_acquire))
						continue;
					// An old pending wake must also respect a newly lowered limit.
					if (index < activeWorkers)
					{
						restoreFpControlState(fpState);
						work();
					}
					// After decrementing, touch ONLY the team's permanent event,
					// never the job. A delayed SetEvent in a later job is harmless:
					// the caller always rechecks the inside count after waking.
					const uint64_t previous = admission.fetch_sub(1, std::memory_order_acq_rel);
					if ((previous & (insideMask | openBit)) == 1)
						SetEvent(done);
					break;
				}
			}
			if (mmcss != nullptr)
				AvRevertMmThreadCharacteristics(mmcss);
		}
	}

	FanOut::Lease FanOut::Lease::acquire()
	{
		const unsigned limit = workerLimit.load(std::memory_order_relaxed);
		const unsigned hardware = std::thread::hardware_concurrency();
		const unsigned count = (std::min)({hardware > 0 ? hardware - 1 : 0, maximumWorkers, limit});
		if (count == 0)
			return {};
		std::lock_guard<std::mutex> lock(lifetimeMutex);
		if (leases == 0)
		{
			auto pending = std::make_unique<Team>();
			pending->start(count);
			takeSlotForLifecycle();
			team = pending.release();
			running.store(count, std::memory_order_release);
			slot.clear(std::memory_order_release);
		}
		++leases;
		return Lease(true);
	}

	FanOut::Lease::~Lease()
	{
		if (held)
			releaseLease();
	}

	FanOut::Lease::Lease(Lease&& other) noexcept : held(std::exchange(other.held, false)) {}

	FanOut::Lease& FanOut::Lease::operator=(Lease&& other) noexcept
	{
		if (this != &other)
		{
			if (held)
				releaseLease();
			held = std::exchange(other.held, false);
		}
		return *this;
	}

#pragma AVRT_CODE_BEGIN
	void FanOut::run(size_t count, Task fn, void* context) noexcept
	{
		const unsigned limit = workerLimit.load(std::memory_order_relaxed);
		if (count < 2 || limit == 0 || running.load(std::memory_order_acquire) == 0
			|| slot.test_and_set(std::memory_order_acquire))
		{
			for (size_t i = 0; i < count; ++i)
				fn(context, i);
			return;
		}
		Team* const current = team;
		if (current == nullptr)
		{
			slot.clear(std::memory_order_release);
			for (size_t i = 0; i < count; ++i)
				fn(context, i);
			return;
		}

		current->count = count;
		current->fn = fn;
		current->context = context;
		current->fpState = captureFpControlState();
		current->next.store(0, std::memory_order_relaxed);
		current->activeWorkers = static_cast<unsigned>((std::min)({count - 1,
			static_cast<size_t>(current->size), static_cast<size_t>(limit)}));
		const uint64_t epoch = current->admission.load(std::memory_order_relaxed) + epochStep;
		current->admission.store(epoch | openBit, std::memory_order_release);
		for (unsigned i = 0; i < current->activeWorkers; ++i)
			SetEvent(current->wake[i]);
		current->work();
		// Closing admission and acquiring inside references share one atomic
		// modification order. Thus every worker either entered before close
		// (and must leave), or cannot touch the context. The epoch prevents ABA.
		current->admission.fetch_and(~openBit, std::memory_order_acq_rel);
		// A fixed 64 PAUSE/YIELD budget, not a block-period spin. Waiting longer
		// burns the MMCSS share (see EngineHostCore.cpp's measured starvation).
		for (unsigned spin = 0; spin < 64; ++spin)
		{
			if ((current->admission.load(std::memory_order_acquire) & insideMask) == 0)
				break;
			YieldProcessor();
		}
		while ((current->admission.load(std::memory_order_acquire) & insideMask) != 0)
		{
			ResetEvent(current->done);
			// Recheck AFTER reset: a worker may have left and signaled before
			// reset. No lost wake; stale signals can only cause an extra loop.
			if ((current->admission.load(std::memory_order_acquire) & insideMask) != 0)
				WaitForSingleObject(current->done, INFINITE);
		}
		slot.clear(std::memory_order_release);
	}

	unsigned FanOut::workerCount() noexcept
	{
		return running.load(std::memory_order_acquire);
	}

	void FanOut::setWorkerLimitForTests(unsigned limit) noexcept
	{
		workerLimit.store((std::min)(limit, maximumWorkers), std::memory_order_relaxed);
	}
#pragma AVRT_CODE_END
}
