/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstddef>
#include <new>
#include <utility>
#include <vector>

#include "runtime/memory/AlignedMemory.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"

// RAII owner for a flat HConvSingle array. The array is a single block
// allocated with AlignedMemory::alloc(sizeof(HConvSingle) * count) and must be
// torn down by closing every successfully initialized element (hcCloseSingle)
// before freeing the block. Counts are stored in the owner rather than read
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

	// Take ownership before initialization starts and value-initialize every C
	// slot to hcCloseSingle's inert state. Initialization may then complete in
	// any order (including on multiple threads); rollback closes every slot,
	// with untouched slots remaining safe no-ops.
	void adoptStorage(AlignedMemory::UniqueAllocation<HConvSingle> newPtr, unsigned newCapacity)
	{
		if (newCapacity != 0 && newPtr == nullptr)
			throw std::bad_alloc();
		reset();
		ptr = std::move(newPtr);
		capacity = newCapacity;
		for (unsigned i = 0; i < capacity; ++i)
			::new (static_cast<void*>(&ptr.get()[i])) HConvSingle{};
	}

	explicit operator bool() const noexcept {return ptr != nullptr;}
	HConvSingle& operator[](unsigned i) const {return ptr.get()[i];}
	HConvSingle* get() const noexcept {return ptr.get();}

	// Close every valid zero-or-initialized slot, then free the whole block.
	void reset();

private:
	AlignedMemory::UniqueAllocation<HConvSingle> ptr;
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
