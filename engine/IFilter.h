/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "runtime/memory/AlignedMemory.h"

#pragma AVRT_VTABLES_BEGIN
class IFilter
{
public:
	virtual ~IFilter() {}

	// These three hooks drive the channel-inheritance protocol in
	// FilterEngine::addFilters; the full contract (what an empty
	// inChannels/outChannels vector means and when it may be emitted) is
	// documented at FilterInfo in FilterConfiguration.h.

	// request to get all channel names instead of selection
	virtual bool getAllChannels() {return false;}
	// return false to request that output and input do not point to the same memory locations
	virtual bool getInPlace() {return true;}
	// request that the channelNames returned by initialize become the new selection
	virtual bool getSelectChannels() {return false;}
	// return value is the channelNames vector, which may contain additional or fewer channel names
	virtual std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) = 0;
	virtual void process(double** output, double** input, unsigned frameCount) = 0;

	// Returns true if silent input can still produce non-silent output, either because
	// the filter carries cross-block state (BiQuad, Delay, IIR) or because it has a
	// convolution tail (Convolution, GraphicEQ). Conservative default is true so new
	// filters opt out of the silent fast-path explicitly.
	virtual bool producesTailFromSilentInput() const { return true; }

protected:
};
#pragma AVRT_VTABLES_END

struct FilterDeleter
{
	void operator()(IFilter* filter) const;
};

using FilterPtr = std::unique_ptr<IFilter, FilterDeleter>;
using FilterVector = std::vector<FilterPtr>;

template<class T, class... Args>
FilterPtr makeFilter(Args&&... args)
{
	return FilterPtr(AlignedMemory::construct<T>(std::forward<Args>(args)...));
}

inline FilterVector singleFilter(FilterPtr filter)
{
	FilterVector filters;
	filters.push_back(std::move(filter));
	return filters;
}
