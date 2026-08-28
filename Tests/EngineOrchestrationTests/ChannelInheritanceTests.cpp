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
*/

#include <vector>

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
}
