/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "engine/ConfigWatcher.h"
#include "engine/IInputTap.h"
#include "platform/windows/Win32Event.h"
#include "platform/windows/Win32Resource.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
	class ConstantInputTap : public IInputTap
	{
	public:
		ConstantInputTap(unsigned targetChannel, double value, bool addsAudio = true)
			: targetChannel(targetChannel), value(value), addsAudio(addsAudio)
		{
		}

		void apply(double* const* channels, unsigned channelCount, unsigned frameCount,
			uint64_t blockToken) noexcept override
		{
			if (callCount < tokens.size())
				tokens[callCount] = blockToken;
			callCount++;
			if (targetChannel >= channelCount)
				return;
			for (unsigned frame = 0; frame < frameCount; frame++)
				channels[targetChannel][frame] += value;
		}

		bool mayAddAudio() const noexcept override
		{
			return addsAudio;
		}

		unsigned targetChannel;
		double value;
		bool addsAudio;
		std::array<uint64_t, 16> tokens{};
		size_t callCount = 0;
	};

	void settleInitialConfiguration(FilterEngine& engine, unsigned channels, unsigned frames)
	{
		std::vector<float> input(static_cast<size_t>(channels) * frames, 0.0f);
		std::vector<float> output(static_cast<size_t>(channels) * frames, 0.0f);
		engine.process(output.data(), input.data(), frames);
	}
}

void testSendBlockCounterCoversEveryProcessPath(test::Harness& harness)
{
	FilterEngine engine;
	float floatSample = 0.0f;
	double doubleSample = 0.0;
	float* floatPlanes[] = {&floatSample};

	harness.expectEqual(engine.blockCounter(), uint64_t(0), "block counter starts at zero");
	engine.process(&floatSample, &floatSample, 0);
	harness.expectEqual(engine.blockCounter(), uint64_t(1), "null-config float bypass increments once");
	engine.process(&doubleSample, &doubleSample, 0);
	harness.expectEqual(engine.blockCounter(), uint64_t(2), "null-config double bypass increments once");
	engine.process(floatPlanes, floatPlanes, 0);
	harness.expectEqual(engine.blockCounter(), uint64_t(3), "null-config planar bypass increments once");

	const std::wstring configPath = writeConfig(harness, L"send-counter-empty.txt", "# empty\n");
	EngineSetup setup = testEngineSetup(48000, 1, 1, 16, configPath);
	setup.sendNamePrefix = L"Local\\EAPO.Send.SeamTest.";
	engine.initialize(setup);
	harness.expectTrue(engine.getHost() == EngineHost::Test, "test engines keep the default host");
	harness.expectTrue(engine.getSendNamePrefix() == setup.sendNamePrefix,
		"initialize stores the Send object-name prefix");
	settleInitialConfiguration(engine, 1, 1);
	const uint64_t beforeBypass = engine.blockCounter();
	engine.process(&floatSample, &floatSample, 1);
	harness.expectEqual(engine.blockCounter(), beforeBypass + 1,
		"empty-config bypass increments exactly once");
}

void testSendTapRunsForEmptyConfigurationAndRestoresBypass(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"send-tap-empty.txt", "# empty\n");
	FilterEngine engine;
	engine.initialize(testEngineSetup(48000, 1, 1, 16, configPath));
	settleInitialConfiguration(engine, 1, 1);

	ConstantInputTap tap(0, 0.75);
	engine.setInputTap(&tap);
	float input[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float output[4] = {};
	engine.process(output, input, 4);
	for (float sample : output)
		harness.expectNear(sample, 0.75, 1e-7, "tap audio reaches an empty configuration's output");
	harness.expectEqual(tap.callCount, size_t(1), "settled empty configuration applies the tap once");

	const size_t callsBeforeDetach = tap.callCount;
	engine.setInputTap(nullptr);
	std::fill_n(output, 4, -1.0f);
	engine.process(output, input, 4);
	harness.expectEqual(tap.callCount, callsBeforeDetach, "detached tap is not called");
	for (float sample : output)
		harness.expectEqual(sample, 0.0f, "detaching the tap restores empty-config bypass output");
}

void testSendTapUsesOneTokenAcrossConfigurationSwap(test::Harness& harness)
{
	const std::wstring configA = writeConfig(harness, L"send-swap-a.txt", "Preamp: 0 dB\n");
	const std::wstring configB = writeConfig(harness, L"send-swap-b.txt", "Preamp: -6 dB\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, configA);
	settleInitialConfiguration(engine, 2, 480);

	ConstantInputTap tap(0, 0.25);
	engine.setInputTap(&tap);
	harness.require(engine.loadConfig(configB), "could not publish the Send seam swap configuration");
	const size_t before = tap.callCount;
	processDcBlock(engine, 0.0f, 0.0f, 32);

	harness.requireEqual(tap.callCount, before + 2,
		"a swapping block applies the tap to current and next configurations");
	harness.expectEqual(tap.tokens[before], tap.tokens[before + 1],
		"both swap configurations receive the same block token");
	harness.expectEqual(tap.tokens[before], engine.blockCounter(),
		"the tap token is the current process block counter");
}

void testSendTapOutputChannelSurvivesVirtualZeroFill(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"send-mono-stereo.txt", "# empty\n");
	FilterEngine engine;
	engine.initialize(testEngineSetup(48000, 1, 2, 16, configPath));

	float settleInput = 0.0f;
	float settleOutput[2] = {};
	engine.process(settleOutput, &settleInput, 1);

	ConstantInputTap tap(1, 2.0);
	engine.setInputTap(&tap);
	float input = 0.5f;
	float output[2] = {};
	engine.process(output, &input, 1);

	harness.expectNear(output[0], 0.5, 1e-7, "mono source remains in output channel zero");
	harness.expectNear(output[1], 2.5, 1e-7,
		"tap target at output index one survives zero fill and follows mono upmix");
}

void testSendTapControlsSilentFastPathState(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"send-state-empty.txt", "# empty\n");
	FilterEngine engine;
	engine.initialize(testEngineSetup(48000, 1, 1, 16, configPath));
	settleInitialConfiguration(engine, 1, 1);
	harness.expectFalse(engine.hasStatefulOrTailFilters(),
		"settled empty configuration has no state or tail");

	ConstantInputTap tap(0, 1.0, true);
	engine.setInputTap(&tap);
	harness.expectTrue(engine.hasStatefulOrTailFilters(),
		"a tap that may add audio keeps the engine off the silent fast path");
	engine.setInputTap(nullptr);
	harness.expectFalse(engine.hasStatefulOrTailFilters(),
		"detaching the tap restores the empty configuration's stateless answer");
}

void testConfigWatcherNamedEventTriggersReload(test::Harness& harness)
{
	const std::wstring eventName = L"Local\\EAPO.Send.ConfigWatcherTest."
		+ std::to_wstring(GetCurrentProcessId());
	winutil::UniqueHandle watchedEvent(CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()));
	if (!watchedEvent)
		harness.fail("could not create the named event for ConfigWatcher");

	std::atomic<int> callbackCount = 0;
	Win32Event shutdown(true, false);
	Win32Event changed(true, false);
	ConfigWatcher watcher(
		shutdown.get(),
		[&] {
			ConfigWatcher::Snapshot snapshot;
			snapshot.eventNames.push_back(eventName);
			return snapshot;
		},
		[&] {
			++callbackCount;
			changed.set();
			return true;
		});
	std::thread worker([&] { watcher.run(); });

	SetEvent(watchedEvent.get());
	const bool observed = WaitForSingleObject(changed.get(), 2000) == WAIT_OBJECT_0;
	Sleep(1200);
	harness.expect(observed, "setting a watched named event triggers a reload callback");
	harness.expectEqual(callbackCount.load(), 1,
		"the watcher resets a named event before taking the reload path");

	shutdown.set();
	worker.join();
}

void runSendSeamTests(test::Harness& harness)
{
	testSendBlockCounterCoversEveryProcessPath(harness);
	testSendTapRunsForEmptyConfigurationAndRestoresBypass(harness);
	testSendTapUsesOneTokenAcrossConfigurationSwap(harness);
	testSendTapOutputChannelSurvivesVirtualZeroFill(harness);
	testSendTapControlsSilentFastPathState(harness);
	testConfigWatcherNamedEventTriggersReload(harness);
}
