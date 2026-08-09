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

#pragma once

#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#ifdef USE_WINDDK
#include <BaseAudioProcessingObject.h>
#else
#define AVRT_VTABLES_BEGIN
#define AVRT_VTABLES_END
#define AVRT_CODE_BEGIN
#define AVRT_CODE_END
#endif

class AlignedMemory
{
public:
	template<class T>
	struct AllocationDeleter
	{
		void operator()(T* ptr) const noexcept
		{
			AlignedMemory::free(ptr);
		}
	};

	template<class T>
	struct ObjectDeleter
	{
		void operator()(T* ptr) const noexcept
		{
			AlignedMemory::destroy(ptr);
		}
	};

	template<class T>
	using UniqueAllocation = std::unique_ptr<T, AllocationDeleter<T>>;

	template<class T>
	using UniqueObject = std::unique_ptr<T, ObjectDeleter<T>>;

	static void* alloc(size_t size);
	static void free(void* ptr);
	// Deterministic failure injection for allocation-path tests. Passing N lets
	// N subsequent allocations succeed and makes the following allocation fail;
	// resetAllocationFailureForTesting() restores normal operation. AlignedMemory
	// is used only while filters/configurations are prepared, never in AVRT_CODE.
	static void failAllocationAfterForTesting(size_t successfulAllocations) noexcept;
	static void resetAllocationFailureForTesting() noexcept;
	// Lifetime observation for tests: successful alloc() calls and non-null
	// free() calls since the last reset. A test binary that links Common.lib
	// whole-archive cannot define its own alloc()/free() to count them any
	// more, so the counters live next to the real definitions instead.
	static size_t allocationCountForTesting() noexcept;
	static size_t freeCountForTesting() noexcept;
	static void resetAllocationCountsForTesting() noexcept;

	// Owns aligned storage for trivially destructible C-style arrays. A null
	// result represents either a size overflow or allocation failure; callers
	// can preserve their existing fallback policy without ever holding a raw
	// allocation between the acquire and commit steps.
	template<class T>
	static UniqueAllocation<T> allocateArray(size_t count) noexcept
	{
		static_assert(std::is_trivially_destructible_v<T>,
			"allocateArray owns storage only; use constructUnique for objects");
		if (count > (std::numeric_limits<size_t>::max)() / sizeof(T))
			return {};
		return UniqueAllocation<T>(static_cast<T*>(alloc(count * sizeof(T))));
	}

	// Typed, checked construction over alloc()/free().
	//
	// alloc() returns nullptr on failure by contract (see
	// docs/ErrorHandlingPolicy.md); a raw "alloc + placement-new" would turn an
	// out-of-memory condition into a null placement-new and a crash. construct()
	// turns the null into a std::bad_alloc instead: the configuration-loading
	// loop catches std::exception and logs it, so OOM surfaces as a logged
	// failure rather than a crash.
	template<class T, class... Args>
	static T* construct(Args&&... args)
	{
		void* mem = alloc(sizeof(T));
		if (mem == nullptr)
			throw std::bad_alloc();
		try
		{
			return ::new(mem) T(std::forward<Args>(args)...);
		}
		catch (...)
		{
			free(mem);
			throw;
		}
	}

	template<class T, class... Args>
	static UniqueObject<T> constructUnique(Args&&... args)
	{
		return UniqueObject<T>(construct<T>(std::forward<Args>(args)...));
	}

	template<class T>
	static void destroy(T* ptr)
	{
		if (ptr != nullptr)
		{
			ptr->~T();
			free(ptr);
		}
	}
};
