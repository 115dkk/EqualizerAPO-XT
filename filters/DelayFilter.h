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

#include <vector>

#include "engine/IFilter.h"
#include "helpers/MemoryHelper.h"

#pragma AVRT_VTABLES_BEGIN
class DelayFilter : public IFilter
{
public:
	DelayFilter(double delay, bool isMs);
	~DelayFilter() override = default;
	bool getInPlace() override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

	double getDelay() const;
	bool getIsMs() const;

private:
	double delay;
	bool isMs;
	unsigned bufferLength = 0;
	unsigned channelCount = 0;
	std::vector<MemoryHelper::UniqueAllocation<double>> buffers;
	unsigned bufferOffset = 0;
};
#pragma AVRT_VTABLES_END
