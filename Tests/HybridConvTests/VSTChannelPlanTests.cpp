/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Table tests for planVstChannels (audit #348 A4): how a VSTPlugin line's
	channels map onto plug-in instances, and every refusal the fill checks
	make, judged without loading a plug-in. Two of the refusals
	(FillSlotCountMismatch, and DuplicateOutputChannel reached only after
	resolution) had no test before the plan was pulled out of
	VSTPluginFilter::initialize.

	These tests link against the same Common.lib as HybridConvTests and run from
	its main() via runVSTChannelPlanTests().
*/

#include <algorithm>
#include <string>
#include <vector>

#include "filters/VSTChannelPlan.h"
#include "Tests/TestHarness.h"

using std::string;
using std::vector;
using std::wstring;

namespace
{
test::Harness harness("VSTChannelPlanTests");

using Refusal = VSTChannelPlan::Refusal;

struct PlanCase
{
	string label;
	VSTChannelPlanRequest request;
	Refusal refusal = Refusal::None;
	// Checked only when refusal == Refusal::None.
	size_t instanceCount = 0;
	size_t paddedChannelCount = 0;
	size_t fillScratchCount = 0;
	vector<int> resolvedInput;
	vector<int> resolvedOutput;
	vector<unsigned> passthrough;
};

VSTChannelPlanRequest request(vector<wstring> channelNames, unsigned inputs, unsigned outputs,
	bool oneContractInstance = false, vector<wstring> inputFill = {}, vector<wstring> outputFill = {})
{
	VSTChannelPlanRequest result;
	result.channelNames = std::move(channelNames);
	result.effectInputCount = inputs;
	result.effectOutputCount = outputs;
	result.oneContractInstance = oneContractInstance;
	result.inputFill = std::move(inputFill);
	result.outputFill = std::move(outputFill);
	return result;
}

void checkCase(const PlanCase& testCase)
{
	const VSTChannelPlan plan = planVstChannels(testCase.request);
	harness.expectEqual(static_cast<int>(plan.refusal), static_cast<int>(testCase.refusal),
		testCase.label + ": refusal");
	if (testCase.refusal != Refusal::None)
	{
		harness.expectFalse(plan.usesFill(), testCase.label + ": a refused plan carries no fill");
		harness.expectTrue(plan.passthroughChannels.empty(), testCase.label + ": a refused plan carries no passthrough");
		return;
	}
	harness.expectEqual(plan.effectChannelCount,
		std::max(testCase.request.effectInputCount, testCase.request.effectOutputCount),
		testCase.label + ": effect channel count");
	harness.expectEqual(plan.instanceCount, testCase.instanceCount, testCase.label + ": instance count");
	harness.expectEqual(plan.paddedChannelCount, testCase.paddedChannelCount, testCase.label + ": padded channel count");
	harness.expectEqual(plan.fillScratchCount, testCase.fillScratchCount, testCase.label + ": fill scratch count");
	harness.expectTrue(plan.resolvedInputChannels == testCase.resolvedInput, testCase.label + ": resolved input slots");
	harness.expectTrue(plan.resolvedOutputChannels == testCase.resolvedOutput, testCase.label + ": resolved output slots");
	harness.expectTrue(plan.passthroughChannels == testCase.passthrough, testCase.label + ": passthrough channels");
	harness.expectEqual(plan.usesFill(), !testCase.resolvedInput.empty() || !testCase.resolvedOutput.empty(),
		testCase.label + ": usesFill");
}
}

void runVSTChannelPlanTests()
{
	const vector<wstring> stereo = {L"L", L"R"};
	const vector<wstring> threeChannels = {L"L", L"R", L"C"};
	const vector<wstring> quad = {L"L", L"R", L"C", L"LFE"};
	const vector<wstring> surround51 = {L"L", L"R", L"C", L"LFE", L"RL", L"RR"};
	const vector<wstring> surround71 = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};

	// PaddingOverflow has no row: instanceCount * effectChannelCount is below
	// channelCount + effectChannelCount by the round-up rule, which a size_t
	// holds for any channel list a request can carry. The check stays in the
	// plan as the guard it was in VSTPluginFilter::initialize.
	const PlanCase cases[] = {
		{"stereo plug-in over 2 channels", request(stereo, 2, 2),
			Refusal::None, 1, 2, 0, {}, {}, {}},
		{"stereo plug-in over 3 channels rounds up to two instances", request(threeChannels, 2, 2),
			Refusal::None, 2, 4, 0, {}, {}, {}},
		{"stereo plug-in over 6 channels", request(surround51, 2, 2),
			Refusal::None, 3, 6, 0, {}, {}, {}},
		{"explicit contract uses one instance", request(surround71, 2, 2, true),
			Refusal::None, 1, 2, 0, {}, {}, {}},
		{"explicit Stereo -> 7.1 contract", request(surround71, 2, 8, true),
			Refusal::None, 1, 8, 0, {}, {}, {}},
		{"no channels on either bus", request(stereo, 0, 0),
			Refusal::NoChannels, 0, 0, 0, {}, {}, {}},
		{"fill across several instances", request(threeChannels, 2, 2, false, {L"L", L"R"}),
			Refusal::FillNeedsOneInstance, 0, 0, 0, {}, {}, {}},
		{"input fill names fewer slots than the bus has", request(stereo, 2, 2, true, {L"L"}),
			Refusal::FillSlotCountMismatch, 0, 0, 0, {}, {}, {}},
		{"output fill names more slots than the bus has", request(stereo, 2, 2, true, {}, {L"L", L"R", L"-"}),
			Refusal::FillSlotCountMismatch, 0, 0, 0, {}, {}, {}},
		{"fill naming an absent channel", request(stereo, 2, 2, true, {L"L", L"Nonexistent"}),
			Refusal::FillChannelMissing, 0, 0, 0, {}, {}, {}},
		{"output fill L,1 resolves twice to channel L", request(stereo, 2, 2, true, {}, {L"L", L"1"}),
			Refusal::DuplicateOutputChannel, 0, 0, 0, {}, {}, {}},
		{"fill with dash slots", request(quad, 2, 2, true, {L"C", L"-"}, {L"L", L"-"}),
			Refusal::None, 1, 2, 2, {2, -1}, {0, -1}, {1, 2, 3}},
		{"input-only fill: the output bus writes the first channels", request(threeChannels, 2, 2, true, {L"L", L"R"}),
			Refusal::None, 1, 2, 0, {0, 1}, {}, {2}},
		{"output fill by number and by name", request(surround71, 2, 2, true, {}, {L"8", L"RL"}),
			Refusal::None, 1, 2, 0, {}, {7, 4}, {0, 1, 2, 3, 5, 6}},
	};

	for (const PlanCase& testCase : cases)
		checkCase(testCase);

	harness.report();
}
