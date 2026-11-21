/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Tests/WavFixtures.h"
#include "engine/LatencyMath.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
void promoteConfiguration(FilterEngine& engine, unsigned channels, unsigned frames)
{
	std::vector<float> input(static_cast<size_t>(channels) * frames, 0.0f);
	std::vector<float> output(input.size(), 0.0f);
	engine.process(output.data(), input.data(), frames);
}

void expectLatency(test::Harness& harness, const FilterEngine& engine,
	std::initializer_list<unsigned> expected, const std::string& label)
{
	const std::vector<unsigned> actual = engine.getRealLatencyByChannel();
	harness.requireEqual(actual.size(), expected.size(), label + " channel count");
	size_t channel = 0;
	unsigned largest = 0;
	for (const unsigned value : expected)
	{
		harness.expectEqual(actual[channel], value,
			label + " channel " + std::to_string(channel));
		largest = (std::max)(largest, value);
		++channel;
	}
	harness.expectEqual(engine.getRealLatency(), largest, label + " maximum");
}

std::wstring writeMonoDeltaIr(test::Harness& harness, const std::wstring& fileName)
{
	const std::wstring path = testDirectory() + L"\\" + fileName;
	std::vector<double> samples(64, 0.0);
	samples[0] = 1.0;
	trackWrittenFile(path);
	harness.require(test::writeWavFile(path, 48000, 1, samples),
		"the real-latency delta IR is written completely");
	return path;
}
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyAdditionSaturates)
{
	const unsigned maximum = (std::numeric_limits<unsigned>::max)();
	harness.expectEqual(addLatency(maximum - 3, 2), maximum - 1,
		"latency addition below the limit remains exact");
	harness.expectEqual(addLatency(maximum - 3, 4), maximum,
		"latency addition saturates instead of wrapping");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyZeroForEmptyAndBiquadChains)
{
	const std::wstring emptyPath = writeConfig(harness, L"real-latency-empty.txt", "# empty\n");
	FilterEngine empty;
	initializeEngine(empty, 48000, 2, 64, emptyPath);
	promoteConfiguration(empty, 2, 64);
	expectLatency(harness, empty, {0, 0}, "empty chain");

	const std::wstring biquadPath = writeConfig(harness, L"real-latency-biquad.txt",
		"Filter: ON PK Fc 1000 Hz Gain -3 dB Q 1\n");
	FilterEngine biquad;
	initializeEngine(biquad, 48000, 2, 64, biquadPath);
	promoteConfiguration(biquad, 2, 64);
	expectLatency(harness, biquad, {0, 0}, "biquad chain");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyTracksSelectedAndConsecutiveDelays)
{
	const std::wstring selectedPath = writeConfig(harness, L"real-latency-selected-delay.txt",
		"Channel: L\n"
		"Delay: 480 samples\n");
	FilterEngine selected;
	initializeEngine(selected, 48000, 2, 64, selectedPath);
	promoteConfiguration(selected, 2, 64);
	expectLatency(harness, selected, {480, 0}, "selected delay");

	// Preamp reuses the Channel filter's output pointer set, and the first Delay
	// reuses Preamp's input pointer set. The second Delay then reuses the first
	// Delay's output set after the non-in-place pointer rotation.
	const std::wstring consecutivePath = writeConfig(harness, L"real-latency-consecutive-delay.txt",
		"Channel: L\n"
		"Preamp: 0 dB\n"
		"Delay: 120 samples\n"
		"Delay: 360 samples\n");
	FilterEngine consecutive;
	initializeEngine(consecutive, 48000, 2, 64, consecutivePath);
	promoteConfiguration(consecutive, 2, 64);
	expectLatency(harness, consecutive, {480, 0}, "consecutive delays");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyFollowsCopyMixesAndConstants)
{
	const std::wstring path = writeConfig(harness, L"real-latency-copy.txt",
		"Channel: L\n"
		"Delay: 480 samples\n"
		"Copy: L=L+R R=0.5\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 64, path);
	promoteConfiguration(engine, 2, 64);
	expectLatency(harness, engine, {480, 0}, "Copy mix and constant");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyFollowsVirtualChannelsThroughCopy)
{
	const std::wstring path = writeConfig(harness, L"real-latency-new-channel.txt",
		"Channel: L\n"
		"Delay: 100 samples\n"
		"NewChannel: VC\n"
		"Copy: VC=L\n"
		"Copy: R=VC\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 64, path);
	promoteConfiguration(engine, 2, 64);
	expectLatency(harness, engine, {100, 100}, "virtual-channel Copy");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyNewChannelKeepsExistingChannelValue)
{
	const std::wstring path = writeConfig(harness, L"real-latency-existing-virtual-channel.txt",
		"Copy: VC=L\n"
		"Channel: VC\n"
		"Delay: 10 samples\n"
		"Channel: L\n"
		"Delay: 100 samples\n"
		"NewChannel: VC\n"
		"Copy: R=VC L=0\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 64, path);
	promoteConfiguration(engine, 2, 64);
	expectLatency(harness, engine, {0, 10}, "existing virtual channel through NewChannel");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyCountsHilbertShiftAndAlignment)
{
	const std::wstring path = writeConfig(harness, L"real-latency-hilbert.txt",
		"Hilbert: Shift=L Align=R Direction=-90\n");
	FilterEngine engine;
	EngineSetup setup = testEngineSetup(48000, 3, 3, 64, path);
	setup.channelMask = 0x7;
	engine.initialize(setup);
	promoteConfiguration(engine, 3, 64);
	expectLatency(harness, engine, {512, 512, 0}, "Hilbert");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyIgnoresConvolutionResponseDelay)
{
	const std::wstring irPath = writeMonoDeltaIr(harness, L"real-latency-ir.wav");
	const std::wstring path = writeConfig(harness, L"real-latency-convolution.txt",
		"Channel: L\n"
		"Delay: 37 samples\n"
		"Convolution: \"" + toNarrow(irPath) + "\"\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 64, path);
	promoteConfiguration(engine, 2, 64);
	expectLatency(harness, engine, {37, 0}, "convolution after delay");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyFollowsMultiConvolutionMappings)
{
	const std::wstring irPath = writeMonoDeltaIr(harness, L"real-latency-multiconvolution-ir.wav");
	const std::wstring path = writeConfig(harness, L"real-latency-multiconvolution.txt",
		"Channel: L\n"
		"Delay: 43 samples\n"
		"MultiConvolution: L=0 \"" + toNarrow(irPath) + "\"\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 64, path);
	promoteConfiguration(engine, 2, 64);
	expectLatency(harness, engine, {43, 0}, "MultiConvolution mapping");
}

TEST_CASE(EngineOrchestrationTests, testRealLatencyCountsSendCompensation)
{
	const std::string target = "{77777777-7777-7777-7777-777777777777}";
	const std::wstring path = writeConfig(harness, L"real-latency-send.txt",
		"Send: " + target + " L=L Compensate=true Latency=960samples\n");
	FilterEngine engine;
	EngineSetup setup = testEngineSetup(48000, 2, 2, 480, path);
	setup.host = EngineHost::Apo;
	setup.deviceGuid = L"{66666666-6666-6666-6666-666666666666}";
	setup.sendNamePrefix = L"Local\\EAPO.RealLatency." +
		std::to_wstring(GetCurrentProcessId()) + L".Send.";
	engine.initialize(setup);
	promoteConfiguration(engine, 2, 480);
	expectLatency(harness, engine, {960, 960}, "Send compensation");
}

TEST_CASE(EngineOrchestrationTests, testEngineRealLatencyFollowsCurrentConfiguration)
{
	const std::wstring firstPath = writeConfig(harness, L"real-latency-current-a.txt",
		"Channel: L\nDelay: 10 samples\n");
	const std::wstring secondPath = writeConfig(harness, L"real-latency-current-b.txt",
		"Channel: R\nDelay: 20 samples\n");

	FilterEngine engine;
	harness.expectEqual(engine.getRealLatency(), 0u,
		"an uninitialized engine reports zero real latency");
	initializeEngine(engine, 100, 2, 8, firstPath);
	harness.expectEqual(engine.getRealLatency(), 0u,
		"a published configuration is not reported before it becomes current");
	promoteConfiguration(engine, 2, 8);
	expectLatency(harness, engine, {10, 0}, "first current configuration");

	harness.require(engine.loadConfig(secondPath), "the second latency configuration loads");
	harness.expectEqual(engine.getRealLatency(), 10u,
		"the old latency remains current while the replacement is pending");
	promoteConfiguration(engine, 2, 8);
	expectLatency(harness, engine, {0, 20}, "replacement current configuration");
}
