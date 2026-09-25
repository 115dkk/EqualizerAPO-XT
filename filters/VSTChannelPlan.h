/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once
#include <cstddef>
#include <string>
#include <vector>

// How a VSTPlugin line's channels map onto plug-in instances, from the counts
// the plug-in reported (audit #348 A4). Pure: no plug-in, no allocation, no
// logging. VSTPluginFilter::initialize builds its instances and buffers from
// it, and the tests (and later the Editor) judge a fill without loading a
// plug-in.
struct VSTChannelPlanRequest
{
	std::vector<std::wstring> channelNames; // the filter's channels
	unsigned effectInputCount = 0;          // per instance, validated
	unsigned effectOutputCount = 0;
	bool oneContractInstance = false;       // busContract && busContract->hasExplicitLayout()
	std::vector<std::wstring> inputFill;    // Input= selectors, empty when none; "-" is an unused slot
	std::vector<std::wstring> outputFill;   // Output= selectors
};

struct VSTChannelPlan
{
	enum class Refusal
	{
		None,
		NoChannels,             // effectChannelCount == 0 (today: pass through without a log line)
		PaddingOverflow,
		FillNeedsOneInstance,
		FillSlotCountMismatch,
		FillChannelMissing,
		DuplicateOutputChannel,
	};

	Refusal refusal = Refusal::None;
	unsigned effectChannelCount = 0;       // max(inputs, outputs)
	size_t instanceCount = 0;              // round-up rule, or 1 for an explicit contract
	size_t paddedChannelCount = 0;
	size_t fillScratchCount = 0;           // one scratch buffer per "-" slot
	std::vector<int> resolvedInputChannels;  // -1 for "-"; empty without a fill
	std::vector<int> resolvedOutputChannels;
	std::vector<unsigned> passthroughChannels; // only with a fill: channels no output slot writes

	bool usesFill() const { return !resolvedInputChannels.empty() || !resolvedOutputChannels.empty(); }
};

VSTChannelPlan planVstChannels(const VSTChannelPlanRequest& request);
