/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include "filters/SendFilter.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "audio/ChannelLayout.h"
#include "diagnostics/performance/PerfProfile.h"
#include "engine/FilterEngine.h"
#include "filters/SendLink.h"
#include "services/logging/Logging.h"

SendFilter::SendFilter(FilterEngine* engine, SendWriter* writer,
	const std::vector<Assignment>& assignments)
	: engine_(engine), writer_(writer), assignments_(assignments)
{
}

std::vector<std::wstring> SendFilter::initialize(float sampleRate, unsigned maxFrameCount,
	std::vector<std::wstring> channelNames)
{
	maxFrameCount_ = maxFrameCount;
	internalAssignments_.clear();
	internalAssignments_.reserve(assignments_.size());
	for (const Assignment& assignment : assignments_)
	{
		std::vector<InternalSummand> prepared;
		prepared.reserve(assignment.sourceSum.size());
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			InternalSummand internal;
			internal.channel = ChannelLayout::getChannelIndex(summand.channel, channelNames, true);
			internal.factor = summand.isDecibel
				? std::pow(10.0, summand.factor / 20.0) : summand.factor;
			prepared.push_back(internal);
		}
		internalAssignments_.push_back(std::move(prepared));
	}

	scratchData_.assign(static_cast<size_t>(assignments_.size()) * maxFrameCount_, 0.0f);
	scratchPlanes_.resize(assignments_.size());
	for (size_t channel = 0; channel < assignments_.size(); channel++)
		scratchPlanes_[channel] = scratchData_.data() + channel * maxFrameCount_;
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void SendFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("SendFilter::process");
	if (writer_ == nullptr || engine_ == nullptr || frameCount > maxFrameCount_)
		return;

	for (size_t assignmentIndex = 0; assignmentIndex < internalAssignments_.size(); assignmentIndex++)
	{
		const std::vector<InternalSummand>& summands = internalAssignments_[assignmentIndex];
		float* const destination = scratchPlanes_[assignmentIndex];
		if (summands.empty())
		{
			std::fill_n(destination, frameCount, 0.0f);
			continue;
		}

		const InternalSummand& first = summands.front();
		for (unsigned frame = 0; frame < frameCount; frame++)
			destination[frame] = static_cast<float>(input[first.channel][frame] * first.factor);
		for (size_t summandIndex = 1; summandIndex < summands.size(); summandIndex++)
		{
			const InternalSummand& summand = summands[summandIndex];
			for (unsigned frame = 0; frame < frameCount; frame++)
			{
				destination[frame] += static_cast<float>(
					input[summand.channel][frame] * summand.factor);
			}
		}
	}

	writer_->write(scratchPlanes_.data(), static_cast<uint32_t>(scratchPlanes_.size()),
		frameCount, engine_->blockCounter());
}
#pragma AVRT_CODE_END

SendCompensationFilter::SendCompensationFilter(uint32_t delayFrames, unsigned outputChannelCount)
	: delayFrames_(delayFrames), requestedOutputChannelCount_(outputChannelCount)
{
}

std::vector<std::wstring> SendCompensationFilter::initialize(float sampleRate,
	unsigned maxFrameCount, std::vector<std::wstring> channelNames)
{
	delayedChannelCount_ = (std::min)(requestedOutputChannelCount_,
		static_cast<unsigned>(channelNames.size()));
	if (!delayLine_.allocate(delayedChannelCount_, delayFrames_, maxFrameCount))
		LogF(L"Send compensation buffer allocation failed; passing audio through");
	channelNames.resize(delayedChannelCount_);
	return channelNames;
}

#pragma AVRT_CODE_BEGIN
void SendCompensationFilter::process(double** output, double** input, unsigned frameCount)
{
	PerfScope _ps("SendCompensationFilter::process");
	if (delayLine_.empty())
	{
		for (unsigned channel = 0; channel < delayedChannelCount_; channel++)
			if (output[channel] != input[channel])
				std::copy_n(input[channel], frameCount, output[channel]);
		return;
	}
	// The routing plan maps this non-in-place filter's outputs to the first
	// delayedChannelCount_ device channels only. Virtual channels are not in the
	// output mapping and therefore keep their current storage untouched.
	delayLine_.process(output, input, frameCount);
}
#pragma AVRT_CODE_END
