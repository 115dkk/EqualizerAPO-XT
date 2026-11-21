/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include <string>
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
	void reportLatency(const std::vector<unsigned>& input, std::vector<unsigned>& output) const override;

	// The device channel IDs a state is compiled against: the channel names,
	// except that a slot whose name the state does not know, but whose
	// 1-based number the state saved as a channel ID, keeps that number.
	// States saved before a device's height channels were named (9 to 12 on
	// a 7.1.4 endpoint, now TFL TFR TRL TRR) hold those numbers.
	static std::vector<std::string> deviceChannelIds(
		const subroute::SubwooferRoutingState& state,
		const std::vector<std::wstring>& channelNames);

private:
	subroute::SubwooferRoutingState state;
	subroute::Processor processor;
	std::vector<subroute::CompiledPath> latencyPaths;
	std::vector<subroute::CompiledOutput> latencyOutputs;
	bool passthrough = true;
	unsigned channelCount = 0;
};
#pragma AVRT_VTABLES_END
