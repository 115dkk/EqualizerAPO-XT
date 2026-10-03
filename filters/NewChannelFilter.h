/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include "engine/IFilter.h"

#pragma AVRT_VTABLES_BEGIN
class NewChannelFilter : public IFilter
{
public:
	NewChannelFilter(std::vector<std::wstring> names, std::vector<std::wstring> alreadyDeclared);
	~NewChannelFilter() override = default;
	bool getAllChannels() override {return false;}
	bool getSelectChannels() override {return true;}
	bool getInPlace() override {return true;}
	bool producesTailFromSilentInput() const override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount,
		std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	std::vector<std::wstring> names;
	std::vector<std::wstring> alreadyDeclared;
};
#pragma AVRT_VTABLES_END
