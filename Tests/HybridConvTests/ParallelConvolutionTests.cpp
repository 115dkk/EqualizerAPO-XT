/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cmath>
#include <cstring>
#include <vector>

#include "filters/ConvolutionFilter.h"
#include "filters/ConvolverProcess.h"
#include "filters/MultiConvolutionFilter.h"
#include "runtime/concurrency/RealtimeFanOut.h"
#include "services/registry/RegistrySettings.h"
#include "services/registry/RegistryPaths.h"
#include "Tests/FakeRegistry.h"
#include "Tests/TestDirectory.h"
#include "Tests/TestHarness.h"
#include "Tests/WavFixtures.h"
#include "dsp/MxcsrGuard.h"

namespace
{
	using eapo::rt::FanOut;
	using Planes = std::vector<std::vector<double>>;

	std::vector<double*> pointers(Planes& planes)
	{
		std::vector<double*> result;
		for (auto& plane : planes)
			result.push_back(plane.data());
		return result;
	}

	template<class Filter>
	void compare(test::Harness& harness, Filter& parallel, Filter& serial,
		unsigned blockSize, const std::vector<std::wstring>& channels)
	{
		FanOut::setWorkerLimitForTests(7);
		const auto outputs = parallel.initialize(48000, blockSize, channels);
		harness.require(FanOut::workerCount() > 0, "the parallel filter acquires a team");
		serial.initialize(48000, blockSize, channels);
		Planes input(channels.size(), std::vector<double>(blockSize));
		Planes actual(outputs.size(), std::vector<double>(blockSize));
		Planes expected = actual;
		auto in = pointers(input);
		auto out = pointers(actual);
		auto ref = pointers(expected);
		MxcsrFtzDazGuard guard;
		for (unsigned block = 0; block < 200; ++block)
		{
			for (size_t c = 0; c < input.size(); ++c)
				for (unsigned f = 0; f < blockSize; ++f)
					input[c][f] = block > 130 ? 0.0 : std::sin((block * blockSize + f) * 0.013 + c) * 0.25;
			FanOut::setWorkerLimitForTests(7);
			parallel.process(out.data(), in.data(), blockSize);
			FanOut::setWorkerLimitForTests(0);
			serial.process(ref.data(), in.data(), blockSize);
			for (size_t c = 0; c < actual.size(); ++c)
				harness.expectTrue(std::memcmp(actual[c].data(), expected[c].data(),
					blockSize * sizeof(double)) == 0, "parallel convolution is bit-identical for every block");
		}
		FanOut::setWorkerLimitForTests(7);
	}
}

TEST_CASE(HybridConvTests, testParallelConvolutionBitIdentity)
{
	test::TestDirectory directory(L"ParallelConvolutionTests");
	const auto path = directory.trackFile(L"long-ir.wav");
	Planes ir(4, std::vector<double>(96000));
	for (size_t c = 0; c < ir.size(); ++c)
		for (size_t f = 0; f < ir[c].size(); ++f)
			ir[c][f] = std::sin(f * 0.071 + c) * std::exp(-static_cast<double>(f) / 16000.0) * 0.01;
	harness.require(test::writeWavFile(path, 48000, ir), "the long multichannel IR is written");
	for (unsigned block : {64u, 480u})
	{
		{
			ConvolutionFilter parallel(ConfigFileReference::target(L"", path).path), serial(ConfigFileReference::target(L"", path).path);
			compare(harness, parallel, serial, block, {L"L", L"R", L"C", L"SUB", L"RL", L"RR", L"SL", L"SR"});
		}
		{
			using Ref = MultiConvolutionCommand::IrChannelRef;
			const std::vector<MultiConvolutionCommand::Mapping> mappings = {
				{L"L", {Ref(0), Ref(1), Ref(2), Ref(3)}},
				{L"R", {Ref(0), Ref(1), Ref(2), Ref(3)}},
				{L"L", {Ref(0, 0.37), Ref(1), Ref(2, -0.25), Ref(3)}},
				{L"virtual", {Ref(0)}}};
			MultiConvolutionFilter parallel(mappings, ConfigFileReference::target(L"", path).path), serial(mappings, ConfigFileReference::target(L"", path).path);
			compare(harness, parallel, serial, block, {L"L", L"R"});
		}
		harness.expectEqual(FanOut::workerCount(), 0u, "filter cleanup releases its lease");
	}
}

TEST_CASE(HybridConvTests, testParallelConvolutionWorkThreshold)
{
	HConvPartitioned prototype{};
	prototype.framelength = 64;
	prototype.head.num_filterbuf = 8;
	prototype.tailfactor = 4;
	harness.expectFalse(useConvolverFanOut(2, 1024, prototype), "short stereo IR stays inline");
	prototype.head.num_filterbuf = 16;
	prototype.tailfactor = 8;
	harness.expectFalse(useConvolverFanOut(8, 8192, prototype), "small 64-frame work stays inline");
	prototype.head.num_filterbuf = 32;
	prototype.tailfactor = 16;
	harness.expectTrue(useConvolverFanOut(8, 32768, prototype), "measured profitable 64-frame work fans out");
	prototype.framelength = 480;
	prototype.head.num_filterbuf = 3;
	prototype.tailfactor = 0;
	harness.expectTrue(useConvolverFanOut(22, 1024, prototype), "many short 480-frame units fan out");
	harness.expectFalse(useConvolverFanOut(1, 1024, prototype), "one unit always stays inline");
}

TEST_CASE(HybridConvTests, testParallelConvolutionRegistryPolicyAndLease)
{
	test::FakeRegistry registry;
	harness.expectTrue(readParallelConvolution(registry), "missing setting enables parallel convolution");
	registry.seedString(APP_REGPATH, L"ParallelConvolution", L"False");
	harness.expectTrue(readParallelConvolution(registry), "only literal false disables parallel convolution");
	registry.seedDword(APP_REGPATH, L"ParallelConvolution", 0);
	harness.expectTrue(readParallelConvolution(registry), "wrong registry type keeps the default");
	registry.seedString(APP_REGPATH, L"ParallelConvolution", L"false");
	harness.expectTrue(!readParallelConvolution(registry), "literal false disables parallel convolution");
	test::TestDirectory directory(L"ParallelConvolutionSettingTests");
	const auto path = directory.trackFile(L"ir.wav");
	harness.require(test::writeWavFile(path, 48000, Planes(2, std::vector<double>(96000, 0.001))), "IR is written");
	ConvolutionFilter convolution(ConfigFileReference::target(L"", path).path, true, readParallelConvolution(registry));
	MultiConvolutionFilter multi({{L"L", {}}, {L"R", {}}}, ConfigFileReference::target(L"", path).path, true, readParallelConvolution(registry));
	convolution.initialize(48000, 480, {L"L", L"R"});
	multi.initialize(48000, 480, {L"L", L"R"});
	harness.expectEqual(FanOut::workerCount(), 0u, "disabled filters never acquire a lease");
	registry.denyRead(APP_REGPATH);
	harness.expectTrue(readParallelConvolution(registry), "unreadable setting keeps the default");
}
