/*
	This file is part of EqualizerAPO-XT.

	EqualizerAPO-XT is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <vector>

#include "SubwooferRouting/Processor.h"
#include "SubwooferRouting/State.h"
#include "engine/IFilter.h"

#pragma AVRT_VTABLES_BEGIN
class SubwooferRoutingFilter : public IFilter
{
public:
	explicit SubwooferRoutingFilter(subroute::SubwooferRoutingState state);
	bool getAllChannels() override { return true; }
	bool getInPlace() override { return false; }
	std::vector<std::wstring> initialize(float sampleRate,
		unsigned maxFrameCount,
		std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	subroute::SubwooferRoutingState state;
	subroute::Processor processor;
	bool passthrough = true;
	unsigned channelCount = 0;
};
#pragma AVRT_VTABLES_END
