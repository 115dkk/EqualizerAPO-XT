/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2013  Jonas Thedering

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
#include <atomic>
#include <limits>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <malloc.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "services/logging/Logging.h"
#include "runtime/memory/AlignedMemory.h"

namespace
{
	constexpr size_t allocationFailureDisabled = (std::numeric_limits<size_t>::max)();
	std::atomic<size_t> allocationFailureCountdown{ allocationFailureDisabled };
	// Deliberately not guarded by _DEBUG: CI builds and runs Release only, so a
	// debug-only counter would leave the observing test with nothing to read.
	// Atomic because configuration preparation can run on several threads at
	// once (AlignedMemory never runs in AVRT_CODE, so the interlocked increment
	// costs nothing on any path where cost matters). Relaxed ordering matches
	// allocationFailureCountdown: the counters carry no data, and the test
	// reads them after the work it measures has finished.
	std::atomic<size_t> successfulAllocationCount{ 0 };
	std::atomic<size_t> completedFreeCount{ 0 };
}

void* AlignedMemory::alloc(size_t size)
{
	size_t remaining = allocationFailureCountdown.load(std::memory_order_relaxed);
	while (remaining != allocationFailureDisabled)
	{
		if (remaining == 0)
		{
			LogFStatic(L"Injected allocation failure for %Iu bytes.", size);
			return nullptr;
		}
		if (allocationFailureCountdown.compare_exchange_weak(
			remaining, remaining - 1, std::memory_order_relaxed, std::memory_order_relaxed))
			break;
	}

#ifdef _DEBUG
	void* memory = _aligned_malloc_dbg(size, 16, __FILE__, __LINE__);
#else
	void* memory = _aligned_malloc(size, 16);
#endif
	if (memory == nullptr)
	{
		LogFStatic(L"Allocation of %Iu bytes failed.", size);
		return nullptr;
	}

	successfulAllocationCount.fetch_add(1, std::memory_order_relaxed);
	return memory;
}

void AlignedMemory::failAllocationAfterForTesting(size_t successfulAllocations) noexcept
{
	allocationFailureCountdown.store(successfulAllocations, std::memory_order_relaxed);
}

void AlignedMemory::resetAllocationFailureForTesting() noexcept
{
	allocationFailureCountdown.store(allocationFailureDisabled, std::memory_order_relaxed);
}

size_t AlignedMemory::allocationCountForTesting() noexcept
{
	return successfulAllocationCount.load(std::memory_order_relaxed);
}

size_t AlignedMemory::freeCountForTesting() noexcept
{
	return completedFreeCount.load(std::memory_order_relaxed);
}

void AlignedMemory::resetAllocationCountsForTesting() noexcept
{
	successfulAllocationCount.store(0, std::memory_order_relaxed);
	completedFreeCount.store(0, std::memory_order_relaxed);
}

void AlignedMemory::free(void* ptr)
{
	// A null free is a no-op, so counting it would make the two totals
	// incomparable: a failed alloc() returns null and is not counted either.
	if (ptr != nullptr)
		completedFreeCount.fetch_add(1, std::memory_order_relaxed);

#ifdef _DEBUG
	_aligned_free_dbg(ptr);
#else
	_aligned_free(ptr);
#endif
}
