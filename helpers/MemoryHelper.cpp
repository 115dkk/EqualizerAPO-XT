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

#include "LogHelper.h"
#include "MemoryHelper.h"

namespace
{
	constexpr size_t allocationFailureDisabled = (std::numeric_limits<size_t>::max)();
	std::atomic<size_t> allocationFailureCountdown{ allocationFailureDisabled };
}

void* MemoryHelper::alloc(size_t size)
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

	return memory;
}

void MemoryHelper::failAllocationAfterForTesting(size_t successfulAllocations) noexcept
{
	allocationFailureCountdown.store(successfulAllocations, std::memory_order_relaxed);
}

void MemoryHelper::resetAllocationFailureForTesting() noexcept
{
	allocationFailureCountdown.store(allocationFailureDisabled, std::memory_order_relaxed);
}

void MemoryHelper::free(void* ptr)
{
#ifdef _DEBUG
	_aligned_free_dbg(ptr);
#else
	_aligned_free(ptr);
#endif
}
