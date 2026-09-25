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

#include "engine/IFilter.h"
#include "ConvolverBank.h"
#include "IrCache.h"

#pragma AVRT_VTABLES_BEGIN
class ConvolutionFilter : public IFilter
{
public:
	ConvolutionFilter(const std::wstring& filename);
	virtual ~ConvolutionFilter();
	// The deferred mute diagnostic's prefix is part of the filter's
	// observable contract (HybridConvTests pins it), like
	// MultiConvolutionFilter's (audit F058t).
	static constexpr const wchar_t* kFrameCountMismatchLogPrefix =
		L"ConvolutionFilter: frameCount";
	bool getInPlace() override { return true; }
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

protected:
	// For subclasses that count and report their mutes under their own name
	// (GraphicEQFilter, audit #348 F3). Stored rather than virtual: the report
	// runs from ~ConvolutionFilter(), when a subclass override is gone.
	ConvolutionFilter(const std::wstring& filename, ConvolverMuteDiagnostics& muteDiagnostics,
		const wchar_t* muteLogPrefix);
	// Builds the units and hands them to bank.install() with frameCount.
	virtual void initializeFilters(unsigned frameCount);
	float sampleRate = 0.0f;
	unsigned channelCount = 0;
	ConvolverBank bank;
	// Keeps the cached impulse response alive for this filter's lifetime. The
	// process-wide cache only holds weak references, so this is what pins the IR
	// in memory while the filter exists. GraphicEQFilter synthesizes its own IR
	// and leaves this null.
	std::shared_ptr<const IrCacheEntry> irEntry;

private:
	void cleanup();

	std::wstring filename;
	ConvolverMuteDiagnostics* muteDiagnostics;
	const wchar_t* muteLogPrefix;
};
#pragma AVRT_VTABLES_END
