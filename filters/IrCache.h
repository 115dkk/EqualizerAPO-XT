/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

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

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "helpers/MemoryHelper.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"

// Decoded impulse-response PCM, shared between filters that reference the same
// IR file. Each live filter keeps a shared_ptr to its entry; the process-wide
// cache (IrCache.cpp) holds only weak references, so an entry survives exactly
// as long as some filter still uses it.
struct IrCacheEntry
{
	unsigned channels = 0;
	unsigned frames = 0;
	// Channel-major IR samples; each inner vector has `frames` elements.
	std::vector<std::vector<double>> buffers;
};

// Loads the impulse response at `filename` (or returns the cached copy, keyed
// by path + mtime + sample rate), validated against the device sample rate.
// Returns nullptr and logs when the file is unreadable, the sample rate does
// not match, or the file has no usable audio (0 frames / 0 channels /
// frames > INT_MAX). One implementation shared by ConvolutionFilter and
// MultiConvolutionFilter so the intake hardening and the cache cannot
// diverge between them.
std::shared_ptr<const IrCacheEntry> loadIrCached(const std::wstring& filename, double sampleRate);

// RAII owner for a flat HConvSingle array. The array is a single block
// allocated with MemoryHelper::alloc(sizeof(HConvSingle) * count) and must be
// torn down by closing every successfully initialized element (hcCloseSingle)
// before freeing the block. Wrapping it makes partial-initialization rollback
// automatic and idempotent. Counts are stored in the owner rather than read
// from a filter member, so teardown does not depend on declaration order.
class HConvSingleArray
{
public:
	HConvSingleArray() = default;
	~HConvSingleArray() { reset(); }

	HConvSingleArray(const HConvSingleArray&) = delete;
	HConvSingleArray& operator=(const HConvSingleArray&) = delete;
	HConvSingleArray(HConvSingleArray&& other) noexcept
		: ptr(std::move(other.ptr)), capacity(other.capacity), initializedCount(other.initializedCount)
	{
		other.capacity = 0;
		other.initializedCount = 0;
	}
	HConvSingleArray& operator=(HConvSingleArray&& other) noexcept
	{
		if (this != &other)
		{
			reset();
			ptr = std::move(other.ptr);
			capacity = other.capacity;
			initializedCount = other.initializedCount;
			other.capacity = 0;
			other.initializedCount = 0;
		}
		return *this;
	}

	// Take ownership of a freshly allocated block holding `newCount` elements.
	// Any previously held block is torn down first using the same
	// close-then-free sequence.
	// Take ownership before initialization starts, then register each element
	// only after hcInitSingle has committed it. If a later initialization
	// throws, reset() closes the completed prefix and never reads untouched
	// allocation bytes.
	void adoptUninitialized(MemoryHelper::UniqueAllocation<HConvSingle> newPtr, unsigned newCapacity)
	{
		reset();
		ptr = std::move(newPtr);
		capacity = newCapacity;
		initializedCount = 0;
	}

	void markInitialized() noexcept
	{
		assert(initializedCount < capacity);
		initializedCount++;
	}

	HConvSingleArray& operator=(std::nullptr_t)
	{
		reset();
		return *this;
	}

	HConvSingle& operator[](unsigned i) const { return ptr.get()[i]; }
	// Implicit decay to the raw pointer lets call sites keep raw-pointer idioms
	// (filters[i], &filters[i], filters == nullptr, hcInitSingle(&filters[i], ...)).
	operator HConvSingle*() const { return ptr.get(); }

	// Close the successfully initialized prefix, then free the whole block.
	void reset();

private:
	MemoryHelper::UniqueAllocation<HConvSingle> ptr;
	unsigned capacity = 0;
	unsigned initializedCount = 0;
};
