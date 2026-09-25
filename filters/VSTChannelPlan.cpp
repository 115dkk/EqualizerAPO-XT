/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include <algorithm>
#include <limits>
#include <utility>

#include "audio/ChannelLayout.h"
#include "VSTChannelPlan.h"

namespace
{
bool checkedMultiply(size_t left, size_t right, size_t& result) noexcept
{
	if (left != 0 && right > (std::numeric_limits<size_t>::max)() / left)
		return false;
	result = left * right;
	return true;
}

// "-" is the only selector the config resolves itself; everything else goes
// through the same name/alias/number lookup the Copy command uses, so a fill
// accepts exactly the channel spellings the rest of a configuration does.
bool resolveChannelFill(const std::vector<std::wstring>& fill,
	const std::vector<std::wstring>& channelNames, std::vector<int>& resolved)
{
	resolved.clear();
	resolved.reserve(fill.size());
	for (const std::wstring& selector : fill)
	{
		if (selector == L"-")
		{
			resolved.push_back(-1);
			continue;
		}
		const int channelIndex = ChannelLayout::getChannelIndex(selector, channelNames);
		if (channelIndex < 0)
			return false;
		resolved.push_back(channelIndex);
	}
	return true;
}

VSTChannelPlan refuse(VSTChannelPlan plan, VSTChannelPlan::Refusal refusal)
{
	plan.refusal = refusal;
	plan.resolvedInputChannels.clear();
	plan.resolvedOutputChannels.clear();
	plan.passthroughChannels.clear();
	return plan;
}
}

VSTChannelPlan planVstChannels(const VSTChannelPlanRequest& request)
{
	VSTChannelPlan plan;
	const size_t channelCount = request.channelNames.size();

	plan.effectChannelCount = (std::max)(request.effectInputCount, request.effectOutputCount);
	if (plan.effectChannelCount == 0)
		return refuse(std::move(plan), VSTChannelPlan::Refusal::NoChannels);

	// round up
	plan.instanceCount = request.oneContractInstance ? 1
		: channelCount / plan.effectChannelCount + (channelCount % plan.effectChannelCount != 0 ? 1 : 0);
	if (!checkedMultiply(plan.instanceCount, plan.effectChannelCount, plan.paddedChannelCount))
		return refuse(std::move(plan), VSTChannelPlan::Refusal::PaddingOverflow);

	if (request.inputFill.empty() && request.outputFill.empty())
		return plan;

	if (plan.instanceCount != 1)
		return refuse(std::move(plan), VSTChannelPlan::Refusal::FillNeedsOneInstance);
	if ((!request.inputFill.empty() && request.inputFill.size() != request.effectInputCount)
		|| (!request.outputFill.empty() && request.outputFill.size() != request.effectOutputCount))
		return refuse(std::move(plan), VSTChannelPlan::Refusal::FillSlotCountMismatch);

	std::vector<int> resolvedInput;
	std::vector<int> resolvedOutput;
	if (!resolveChannelFill(request.inputFill, request.channelNames, resolvedInput)
		|| !resolveChannelFill(request.outputFill, request.channelNames, resolvedOutput))
		return refuse(std::move(plan), VSTChannelPlan::Refusal::FillChannelMissing);

	std::vector<bool> writtenChannels(channelCount, false);
	if (resolvedOutput.empty())
	{
		for (unsigned channel = 0; channel < request.effectOutputCount && channel < channelCount; channel++)
			writtenChannels[channel] = true;
	}
	else
	{
		for (int channelIndex : resolvedOutput)
		{
			if (channelIndex < 0)
				continue;
			if (writtenChannels[static_cast<size_t>(channelIndex)])
				return refuse(std::move(plan), VSTChannelPlan::Refusal::DuplicateOutputChannel);
			writtenChannels[static_cast<size_t>(channelIndex)] = true;
		}
	}

	// Every "-" slot is handed its own scratch buffer, so the fills add to the
	// padding buffers the filter allocates.
	for (int channelIndex : resolvedInput)
	{
		if (channelIndex < 0)
			plan.fillScratchCount++;
	}
	for (int channelIndex : resolvedOutput)
	{
		if (channelIndex < 0)
			plan.fillScratchCount++;
	}

	plan.passthroughChannels.reserve(channelCount);
	for (size_t channel = 0; channel < channelCount; channel++)
	{
		if (!writtenChannels[channel])
			plan.passthroughChannels.push_back(static_cast<unsigned>(channel));
	}
	plan.resolvedInputChannels = std::move(resolvedInput);
	plan.resolvedOutputChannels = std::move(resolvedOutput);
	return plan;
}
