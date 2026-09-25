/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The consumer half of the channel-inheritance contract (audit #250 A5,
	documented at FilterInfo in FilterConfiguration.h): an empty
	inChannels/outChannels vector means "reuse the pointer set the previous
	filter left behind", and the non-in-place buffer rotation is exactly why
	an empty outChannels is only sound between in-place neighbours. Until
	FilterConfiguration became directly constructible (A2) this protocol was
	guarded only by golden output hashes, which cannot distinguish
	"inherited the pointers" from "recomputed identical indices".

	The producer half - which filter gets empty vectors in the first place -
	lives in ChannelRoutingPlan since audit #348 F1, and the cases at the end
	drive it directly instead of through a config file and output hashes.
*/

#include <string>
#include <vector>

#include "engine/ChannelRoutingPlan.h"
#include "engine/FilterConfiguration.h"
#include "Tests/TestHarness.h"

namespace
{
// Records the pointer sets process() hands it; the recording IS the
// assertion surface.
class RecordingFilter : public IFilter
{
public:
	explicit RecordingFilter(bool inPlace)
		: inPlace(inPlace)
	{
	}

	bool getInPlace() override
	{
		return inPlace;
	}

	std::vector<std::wstring> initialize(float, unsigned,
		std::vector<std::wstring> channelNames) override
	{
		return channelNames;
	}

	void process(double** output, double** input, unsigned) override
	{
		seenInput.assign(input, input + 2);
		seenOutput.assign(output, output + 2);
	}

	bool inPlace;
	std::vector<double*> seenInput;
	std::vector<double*> seenOutput;
};

std::unique_ptr<FilterInfo> makeInfo(RecordingFilter*& outFilter, bool inPlace,
	std::vector<size_t> inChannels, std::vector<size_t> outChannels)
{
	auto info = std::make_unique<FilterInfo>();
	outFilter = AlignedMemory::construct<RecordingFilter>(inPlace);
	info->filter = FilterPtr(outFilter);
	info->inPlace = inPlace;
	info->inChannels = std::move(inChannels);
	info->outChannels = std::move(outChannels);
	return info;
}

using Names = std::vector<std::wstring>;
using Indices = std::vector<size_t>;

// What addFilters records for one filter, replayed against the plan: the
// names the filter was initialized with and the two index vectors.
struct RoutedFilter
{
	Names initializedWith;
	Indices inChannels;
	Indices outChannels;
};

// One addFilters step. An empty `returns` stands for a filter whose
// initialize() hands its channel names back unchanged, like most filters.
RoutedFilter route(ChannelRoutingPlan& plan, bool inPlace, bool allChannels = false,
	bool selectChannels = false, const Names& returns = {})
{
	RoutedFilter routed;
	ChannelRoutingPlan::Entry entry = plan.enter(allChannels);
	routed.initializedWith = entry.initializeWith;
	routed.inChannels = std::move(entry.inChannels);
	const Names newNames = returns.empty() ? routed.initializedWith : returns;
	routed.outChannels = plan.leave(newNames, inPlace, selectChannels);
	return routed;
}

void runChannelRoutingPlanTests(test::Harness& harness)
{
	const Names stereo = { L"L", L"R" };

	// A chain of in-place filters over the same names: the first places its
	// buffers, every later one inherits both sides.
	{
		ChannelRoutingPlan plan;
		plan.begin(stereo, false);
		const RoutedFilter first = route(plan, true);
		const RoutedFilter second = route(plan, true);
		const RoutedFilter third = route(plan, true);

		harness.expect(first.initializedWith == stereo, "the first filter is initialized with the device channels");
		harness.expect(first.inChannels == Indices({ 0, 1 }), "the first filter's inputs are placed by name");
		harness.expect(first.outChannels == Indices({ 0, 1 }), "the first filter's outputs are placed by name");
		harness.expect(second.inChannels.empty() && second.outChannels.empty(),
			"an in-place successor over the same names inherits inputs and outputs");
		harness.expect(third.inChannels.empty() && third.outChannels.empty(),
			"the inheritance holds along the whole in-place chain");
	}

	// A non-in-place filter rotates the buffer sets: it still reads what the
	// previous filter left, but writes into placed outputs, and its successor
	// reads those while placing its own outputs again.
	{
		ChannelRoutingPlan plan;
		plan.begin(stereo, false);
		route(plan, true);
		const RoutedFilter rotating = route(plan, false);
		const RoutedFilter successor = route(plan, true);

		harness.expect(rotating.inChannels.empty(), "a non-in-place filter reads the buffers its predecessor left");
		harness.expect(rotating.outChannels == Indices({ 0, 1 }),
			"a non-in-place filter places its outputs instead of inheriting them");
		harness.expect(successor.inChannels.empty(), "after the rotation the successor reads the rotated buffers");
		harness.expect(successor.outChannels == Indices({ 0, 1 }),
			"an in-place filter after a non-in-place one does not inherit outputs");
	}

	// A filter that creates a channel appends it to the list once; a later
	// filter naming it again finds it by name.
	{
		ChannelRoutingPlan plan;
		plan.begin(stereo, false);
		const RoutedFilter creating = route(plan, false, false, true, { L"L", L"R", L"X" });
		const RoutedFilter reusing = route(plan, false, false, true, { L"X", L"L" });

		harness.expect(creating.outChannels == Indices({ 0, 1, 2 }), "a new channel name is placed after the device channels");
		harness.expect(reusing.outChannels == Indices({ 2, 0 }), "a known channel name is placed at its existing index");
		harness.expect(plan.allChannelNames() == Names({ L"L", L"R", L"X" }),
			"a new channel name is appended to the channel list exactly once");
	}

	// A Channel-like filter selects channels: the next filter is initialized
	// with its names and reads only their buffers.
	{
		ChannelRoutingPlan plan;
		plan.begin(stereo, false);
		route(plan, true);
		route(plan, true, false, true, { L"R" });
		const RoutedFilter next = route(plan, true);

		harness.expect(plan.currentChannelNames() == Names({ L"R" }), "a selecting filter changes the current selection");
		harness.expect(next.initializedWith == Names({ L"R" }), "the next filter sees the selected names");
		harness.expect(next.inChannels == Indices({ 1 }), "the next filter reads only the selected channel's buffer");
	}

	// A getAllChannels filter is initialized with every channel, and the
	// selection in force before it is restored for the filter after it.
	{
		ChannelRoutingPlan plan;
		plan.begin(stereo, false);
		route(plan, true, false, true, { L"L" });
		route(plan, true);
		const RoutedFilter wide = route(plan, false, true);
		const RoutedFilter after = route(plan, true);

		harness.expect(wide.initializedWith == stereo, "a getAllChannels filter is initialized with every channel");
		harness.expect(wide.inChannels == Indices({ 0, 1 }), "a getAllChannels filter reads every channel's buffer");
		harness.expect(plan.currentChannelNames() == Names({ L"L" }), "the selection is restored after a getAllChannels filter");
		harness.expect(after.initializedWith == Names({ L"L" }), "the filter after it sees the restored selection");
	}

	// The in-place-ness of the last filter carries into the next load, and
	// begin() starts every other part of the plan afresh.
	{
		ChannelRoutingPlan plan;
		plan.begin(stereo, false);
		route(plan, false, false, true, { L"L", L"R", L"X" });
		route(plan, true);
		harness.expect(plan.lastInPlace(), "a load whose last filter is in place ends in place");

		plan.begin(stereo, plan.lastInPlace());
		harness.expect(plan.lastInPlace(), "begin() carries the previous load's lastInPlace");
		harness.expect(plan.allChannelNames() == stereo && plan.currentChannelNames() == stereo,
			"begin() drops the previous load's channels and selection");
		const RoutedFilter first = route(plan, true);
		harness.expect(first.inChannels == Indices({ 0, 1 }), "the first filter of a new load places its inputs");

		plan.begin(stereo, false);
		harness.expect(!plan.lastInPlace(), "begin() takes a previous load that ended not in place as such");
	}
}
}

void runChannelInheritanceTests(test::Harness& harness)
{
	// An in-place successor with empty vectors sees exactly the pointer set
	// its predecessor saw - inheritance, not recomputation.
	{
		RecordingFilter* first = nullptr;
		RecordingFilter* second = nullptr;
		std::vector<std::unique_ptr<FilterInfo>> infos;
		infos.push_back(makeInfo(first, true, { 0, 1 }, { 0, 1 }));
		infos.push_back(makeInfo(second, true, {}, {}));

		FilterConfiguration config(EngineStreamFormat{ 2, 2, 64 },
			std::move(infos), 2);
		std::vector<double> block(2 * 64, 0.25);
		config.read(block.data(), 64);
		config.process(64);

		harness.require(first->seenInput.size() == 2 && second->seenInput.size() == 2,
			"both recording filters ran");
		harness.expect(second->seenInput == first->seenInput,
			"empty inChannels inherits the predecessor's input pointers");
		harness.expect(second->seenOutput == first->seenOutput,
			"empty outChannels between in-place filters inherits the output pointers");
	}

	// The non-in-place rotation: the successor's inputs are the predecessor's
	// OUTPUT buffers, which is why an empty outChannels may not follow a
	// non-in-place filter - the pointers it would reuse were just swapped.
	{
		RecordingFilter* first = nullptr;
		RecordingFilter* second = nullptr;
		std::vector<std::unique_ptr<FilterInfo>> infos;
		infos.push_back(makeInfo(first, false, { 0, 1 }, { 0, 1 }));
		infos.push_back(makeInfo(second, true, { 0, 1 }, { 0, 1 }));

		FilterConfiguration config(EngineStreamFormat{ 2, 2, 64 },
			std::move(infos), 2);
		std::vector<double> block(2 * 64, 0.25);
		config.read(block.data(), 64);
		config.process(64);

		harness.require(first->seenInput.size() == 2 && second->seenInput.size() == 2,
			"both recording filters ran (rotation case)");
		harness.expect(first->seenOutput != first->seenInput,
			"a non-in-place filter writes into the second buffer set");
		harness.expect(second->seenInput == first->seenOutput,
			"after the rotation the successor reads the predecessor's output buffers");
	}

	runChannelRoutingPlanTests(harness);
}
