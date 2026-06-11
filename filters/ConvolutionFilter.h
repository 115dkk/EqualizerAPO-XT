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

#include "IFilter.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"

struct IrCacheEntry;

// RAII owner for the per-channel HConvSingle array. The array is a single block
// allocated with MemoryHelper::alloc(sizeof(HConvSingle) * channelCount) and must
// be torn down by closing every channel filter (hcCloseSingle) before freeing the
// block. Wrapping it makes that teardown automatic and idempotent. The holder keeps
// a reference to the owner's channelCount so it knows how many channel filters to
// close; the count is read at teardown time, after the owner has set it. The type
// stays a thin handle around HConvSingle*: it converts to the raw pointer and is
// assignable from one so existing call sites (filters[i], &filters[i],
// filters = MemoryHelper::alloc(...), filters == nullptr) keep their exact meaning.
class HConvSingleArray
{
public:
	explicit HConvSingleArray(const unsigned& channelCount)
		: ptr(nullptr), channelCount(channelCount) {}
	~HConvSingleArray() { reset(); }

	HConvSingleArray(const HConvSingleArray&) = delete;
	HConvSingleArray& operator=(const HConvSingleArray&) = delete;

	// Take ownership of a freshly allocated block (or nullptr). Any previously held
	// block is torn down first using the same close-then-free sequence.
	HConvSingleArray& operator=(HConvSingle* newPtr)
	{
		if (newPtr != ptr)
			reset();
		ptr = newPtr;
		return *this;
	}

	HConvSingleArray& operator=(std::nullptr_t)
	{
		reset();
		return *this;
	}

	HConvSingle& operator[](unsigned i) const { return ptr[i]; }
	// Implicit decay to the raw pointer keeps every existing use of `filters`
	// (filters[i], &filters[i], filters == nullptr, hcInitSingle(&filters[i], ...))
	// byte-for-byte identical to the former raw HConvSingle* member.
	operator HConvSingle*() const { return ptr; }

	// Close every channel filter, then free the block. Mirrors the legacy
	// cleanup() sequence exactly (hcCloseSingle loop, then MemoryHelper::free).
	void reset();

private:
	HConvSingle* ptr;
	const unsigned& channelCount;
};

#pragma AVRT_VTABLES_BEGIN
class ConvolutionFilter : public IFilter
{
public:
	ConvolutionFilter(const std::wstring& filename);
	virtual ~ConvolutionFilter();
	bool getInPlace() override { return true; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

protected:
	virtual void initializeFilters(unsigned frameCount);
	float sampleRate = 0.0f;
	unsigned channelCount = 0;
	// Declared after channelCount: the holder binds a reference to channelCount in
	// the initializer list, so channelCount must be constructed first.
	HConvSingleArray filters;
	// Keeps the cached impulse response alive for this filter's lifetime. The
	// process-wide cache only holds weak references, so this is what pins the IR
	// in memory while the filter exists. GraphicEQFilter synthesizes its own IR
	// and leaves this null.
	std::shared_ptr<const IrCacheEntry> irEntry;

private:
	void cleanup();

	std::wstring filename;
	unsigned maxFrameCount;
	unsigned filterFrameCount;
	bool frameCountMismatchLogged;
};
#pragma AVRT_VTABLES_END
