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
#include <string>
#include <vector>

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
// diverge between them. (audit #146 TD001)
std::shared_ptr<const IrCacheEntry> loadIrCached(const std::wstring& filename, double sampleRate);

// RAII owner for a flat HConvSingle array. The array is a single block
// allocated with MemoryHelper::alloc(sizeof(HConvSingle) * count) and must be
// torn down by closing every element (hcCloseSingle) before freeing the block.
// Wrapping it makes that teardown automatic and idempotent. The element count
// is stored by value at adopt() time (audit #146 TD023 replaced the previous
// reference-to-owner-member binding, which depended on member declaration
// order). The type stays a thin handle around HConvSingle*: it converts to the
// raw pointer, so existing call sites (filters[i], &filters[i],
// filters == nullptr) keep their exact meaning.
class HConvSingleArray
{
public:
	HConvSingleArray() = default;
	~HConvSingleArray() { reset(); }

	HConvSingleArray(const HConvSingleArray&) = delete;
	HConvSingleArray& operator=(const HConvSingleArray&) = delete;

	// Take ownership of a freshly allocated block holding `newCount` elements.
	// Any previously held block is torn down first using the same
	// close-then-free sequence.
	void adopt(HConvSingle* newPtr, unsigned newCount)
	{
		if (newPtr != ptr)
			reset();
		ptr = newPtr;
		count = newCount;
	}

	HConvSingleArray& operator=(std::nullptr_t)
	{
		reset();
		return *this;
	}

	HConvSingle& operator[](unsigned i) const { return ptr[i]; }
	// Implicit decay to the raw pointer keeps every existing use of `filters`
	// (filters[i], &filters[i], filters == nullptr, hcInitSingle(&filters[i], ...))
	// byte-for-byte identical to a raw HConvSingle* member.
	operator HConvSingle*() const { return ptr; }

	// Close every element, then free the block. Mirrors the legacy cleanup()
	// sequence exactly (hcCloseSingle loop, then MemoryHelper::free).
	void reset();

private:
	HConvSingle* ptr = nullptr;
	unsigned count = 0;
};
