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

#include <string>
#include <utility>
#include <vector>

#include "engine/IFilter.h"
#include "CopyCommand.h"

#pragma AVRT_VTABLES_BEGIN
class CopyFilter : public IFilter
{
public:
	CopyFilter(const std::vector<Assignment>& assignments);
	~CopyFilter() override = default;
	bool getAllChannels() override {return true;}
	bool getInPlace() override {return false;}
	bool producesTailFromSilentInput() const override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount, std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	std::vector<Assignment> assignments;

	struct InternalSummand
	{
		int channel;
		double factor;

		InternalSummand(int channel, double factor) noexcept
			: channel(channel), factor(factor)
		{
		}
	};

	struct InternalAssignment
	{
		int targetChannel;
		std::vector<InternalSummand> sourceSum;

		InternalAssignment(int targetChannel, std::vector<InternalSummand> sourceSum)
			: targetChannel(targetChannel), sourceSum(std::move(sourceSum))
		{
		}
	};

	std::vector<InternalAssignment> internalAssignments;
	size_t inputChannelCount = 0;
	bool hasNonzeroConstant = false;
};
#pragma AVRT_VTABLES_END
