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

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "runtime/memory/MemoryHelper.h"
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
		: ptr(std::move(other.ptr)), capacity(other.capacity)
	{
		other.capacity = 0;
	}
	HConvSingleArray& operator=(HConvSingleArray&& other) noexcept
	{
		if (this != &other)
		{
			reset();
			ptr = std::move(other.ptr);
			capacity = other.capacity;
			other.capacity = 0;
		}
		return *this;
	}

	// Take ownership of a freshly allocated block holding `newCount` elements.
	// Any previously held block is torn down first using the same
	// close-then-free sequence.
	// Take ownership before initialization starts and value-initialize every C
	// slot to hcCloseSingle's inert state. Initialization may then complete in
	// any order (including on multiple threads); rollback closes every slot,
	// with untouched slots remaining safe no-ops.
	void adoptStorage(MemoryHelper::UniqueAllocation<HConvSingle> newPtr, unsigned newCapacity)
	{
		if (newCapacity != 0 && newPtr == nullptr)
			throw std::bad_alloc();
		reset();
		ptr = std::move(newPtr);
		capacity = newCapacity;
		for (unsigned i = 0; i < capacity; ++i)
			::new (static_cast<void*>(&ptr.get()[i])) HConvSingle{};
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

	// Close every valid zero-or-initialized slot, then free the whole block.
	void reset();

private:
	MemoryHelper::UniqueAllocation<HConvSingle> ptr;
	unsigned capacity = 0;
};

struct ConvolverUnitSource
{
	const double* samples = nullptr;
	unsigned sampleCount = 0;
	unsigned prototype = 0;
};

// Builds independent HConvSingle processing states while sharing immutable
// filter banks between units that name the same prototype. Planning,
// allocation, parallel prototype transforms, fan-out and rollback live here
// so all convolution filters follow one construction contract.
HConvSingleArray buildConvolverArray(const std::vector<ConvolverUnitSource>& sources,
	unsigned frameCount);
