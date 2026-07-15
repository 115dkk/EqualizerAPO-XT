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

#include <new>
#include <utility>

#ifdef USE_WINDDK
#include <BaseAudioProcessingObject.h>
#else
#define AVRT_VTABLES_BEGIN
#define AVRT_VTABLES_END
#define AVRT_CODE_BEGIN
#define AVRT_CODE_END
#endif

class MemoryHelper
{
public:
	static void* alloc(size_t size);
	static void free(void* ptr);
	// Deterministic failure injection for allocation-path tests. Passing N lets
	// N subsequent allocations succeed and makes the following allocation fail;
	// resetAllocationFailureForTesting() restores normal operation. MemoryHelper
	// is used only while filters/configurations are prepared, never in AVRT_CODE.
	static void failAllocationAfterForTesting(size_t successfulAllocations) noexcept;
	static void resetAllocationFailureForTesting() noexcept;

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
