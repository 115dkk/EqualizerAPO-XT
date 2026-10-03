/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>
#include <vector>

#include "dsp/DelayLine.h"
#include "engine/IFilter.h"
#include "filters/CopyFilter.h"

class FilterEngine;
class SendWriter;

#pragma AVRT_VTABLES_BEGIN
class SendFilter : public IFilter
{
public:
	SendFilter(FilterEngine* engine, SendWriter* writer,
		const std::vector<Assignment>& assignments);
	~SendFilter() override = default;

	bool getAllChannels() override {return true;}
	bool producesTailFromSilentInput() const override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount,
		std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	struct InternalSummand
	{
		int channel = -1;
		double factor = 0.0;
	};

	FilterEngine* engine_ = nullptr;
	SendWriter* writer_ = nullptr;
	std::vector<Assignment> assignments_;
	std::vector<std::vector<InternalSummand>> internalAssignments_;
	std::vector<float> scratchData_;
	std::vector<float*> scratchPlanes_;
	unsigned maxFrameCount_ = 0;
};

class SendCompensationFilter : public IFilter
{
public:
	SendCompensationFilter(uint32_t delayFrames, unsigned outputChannelCount);
	~SendCompensationFilter() override = default;

	bool getAllChannels() override {return true;}
	bool getInPlace() override {return false;}
	std::vector<std::wstring> initialize(float sampleRate, unsigned maxFrameCount,
		std::vector<std::wstring> channelNames) override;
	void process(double** output, double** input, unsigned frameCount) override;

private:
	uint32_t delayFrames_ = 0;
	unsigned requestedOutputChannelCount_ = 0;
	unsigned delayedChannelCount_ = 0;
	DelayLine delayLine_;
};
#pragma AVRT_VTABLES_END
