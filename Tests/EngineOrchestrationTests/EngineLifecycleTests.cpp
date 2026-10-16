/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The engine's life around its configurations: process() before or without
	one, the first load's publication channel, the swap channel's permit,
	channel expansion under an empty configuration, the crossfade on reload,
	a failed reload that keeps the active configuration, and the config
	watcher (directory backoff, path changes, watched registry keys).
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include "engine/ConfigSwapChannel.h"
#include "engine/ConfigWatcher.h"
#include "platform/windows/Win32Event.h"
#include "runtime/memory/AlignedMemory.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
template<typename Predicate>
bool waitForCondition(Predicate&& predicate, std::chrono::milliseconds timeout)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (predicate())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return predicate();
}
}

// process() can arrive before initialize(), and initialize() can finish
// without loading any configuration (unreadable ConfigPath and no custom
// path). Both leave currentConfig null; every overload must pass audio
// through instead of dereferencing it inside audiodg.exe.
void testProcessWithoutConfigurationDoesNotCrash(test::Harness& harness)
{
	FilterEngine engine;

	const unsigned frames = 16;
	std::vector<float> inputF((size_t)2 * frames, 0.25f);
	std::vector<float> outputF((size_t)2 * frames, -1.0f);
	engine.process(outputF.data(), inputF.data(), frames);

	std::vector<double> inputD((size_t)2 * frames, 0.25);
	std::vector<double> outputD((size_t)2 * frames, -1.0);
	engine.process(outputD.data(), inputD.data(), frames);

	std::vector<float> planarFIn((size_t)2 * frames, 0.25f);
	std::vector<float> planarFOut((size_t)2 * frames, -1.0f);
	float* inF[2] = {planarFIn.data(), planarFIn.data() + frames};
	float* outF[2] = {planarFOut.data(), planarFOut.data() + frames};
	engine.process(outF, inF, frames);

	// Reaching this line is the point: no null dereference. Before
	// initialize() the channel counts are zero, so the bypass copies nothing
	// and the output buffers stay untouched.
	harness.expect(outputF[0] == -1.0f && outputD[0] == -1.0 && planarFOut[0] == -1.0f,
		"process() without a configuration wrote output despite zero channel counts");
}

// initialize() seeds an empty active configuration and publishes the requested
// file through the same worker->RT channel used by every later reload. Before
// the first audio block consumes it, the public state query must conservatively
// report the pending transition instead of treating the config as directly
// installed.
void testInitialLoadUsesPublicationChannel(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"initial-publication.txt",
		"Preamp: -6.0206 dB\n");

	FilterEngine engine;
	const std::wstring deviceName = L"EngineOrchestrationTests";
	EngineSetup setup;
	setup.inputChannelCount = 2;
	setup.realChannelCount = 2;
	setup.outputChannelCount = 2;
	setup.maxFrameCount = 16;
	setup.customPath = configPath;
	setup.deviceName = deviceName;
	setup.connectionName = L"File";
	engine.initialize(setup);

	harness.expect(engine.hasStatefulOrTailFilters(),
		"initial configuration bypassed the worker-to-RT publication channel");
}

void testConfigSwapChannelPermitRoundTrip(test::Harness& harness)
{
	using TestChannel = ConfigSwapChannel<std::unique_ptr<int>>;

	TestChannel channel;
	channel.reset(std::make_unique<int>(1));
	harness.require(channel.acquirePublishPermit(0),
		"fresh config channel did not grant its producer permit");
	channel.publish(std::make_unique<int>(2));
	harness.expect(channel.hasPending(), "published config was not visible to the RT side");
	harness.expectEqual(*channel.current(), 1, "publish replaced current before RT acquisition");

	channel.completeTransition();
	harness.expectFalse(channel.hasPending(), "completed transition stayed pending");
	harness.expectEqual(*channel.current(), 2, "completed transition did not promote pending config");
	harness.require(channel.acquirePublishPermit(0),
		"RT completion did not return the producer permit");
	channel.releasePublishPermit();

	harness.require(channel.acquirePublishPermit(0),
		"config channel did not grant a permit before reset");
	channel.publish(std::make_unique<int>(3));
	channel.reset(std::make_unique<int>(4));
	harness.expectFalse(channel.hasPending(), "reset kept a discarded pending config");
	harness.expectEqual(*channel.current(), 4, "reset did not install its seed config");
	harness.require(channel.acquirePublishPermit(0),
		"reset discarded a pending config without returning its permit");
	channel.releasePublishPermit();
}

// Audit #422 E5: loadConfig owns the publication permit. A second producer
// waits until the audio thread consumes the pending initial configuration
// instead of replacing that slot.
void testLoadConfigOwnsPublicationPermit(test::Harness& harness)
{
	const std::wstring configA = writeConfig(harness, L"permit-a.txt", "Preamp: -3 dB\n");
	const std::wstring configB = writeConfig(harness, L"permit-b.txt", "Preamp: -9 dB\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 16, configA);

	Win32Event reloadStarted(true, false);
	Win32Event reloadCompleted(true, false);
	std::atomic<bool> loaded = false;
	std::thread reload([&] {
		reloadStarted.set();
		loaded.store(engine.loadConfig(configB));
		reloadCompleted.set();
	});
	harness.require(WaitForSingleObject(reloadStarted.get(), 2000) == WAIT_OBJECT_0,
		"the second configuration loader starts");
	harness.expect(WaitForSingleObject(reloadCompleted.get(), 20) == WAIT_TIMEOUT,
		"the second configuration waits while the initial publication is pending");

	processDcBlock(engine, 1.0f, 1.0f, 16);
	const bool completed = WaitForSingleObject(reloadCompleted.get(), 2000) == WAIT_OBJECT_0;
	harness.expect(completed,
		"consuming the initial publication releases the second configuration loader");
	reload.join();
	harness.expect(loaded.load(), "the waiting configuration load publishes successfully");
}

// Windows may give a render APO fewer input channels than the endpoint output
// layout (for example, a stereo application stream feeding an 8-channel
// endpoint). An empty configuration is still responsible for adapting that
// connection: preserve the real input channels and silence the extra outputs.
void testEmptyConfigurationExpandsRenderChannels(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"empty-channel-expansion.txt",
		"# All filters are disabled\n");

	FilterEngine engine;
	const std::wstring deviceName = L"EngineOrchestrationTests";
	EngineSetup setup;
	setup.inputChannelCount = 2;
	setup.realChannelCount = 2;
	setup.outputChannelCount = 8;
	setup.maxFrameCount = 16;
	setup.customPath = configPath;
	setup.deviceName = deviceName;
	setup.connectionName = L"File";
	engine.initialize(setup);

	constexpr unsigned frames = 4;
	float input[frames * 2] = {
		0.25f, -0.5f,
		0.5f, -0.25f,
		0.75f, 0.125f,
		1.0f, 0.0f
	};
	auto expectExpanded = [&](const float* output, const std::string& layout) {
		for (unsigned frame = 0; frame < frames; ++frame)
		{
			harness.expectEqual(output[frame * 8], input[frame * 2],
				layout + " preserves the left input channel while expanding");
			harness.expectEqual(output[frame * 8 + 1], input[frame * 2 + 1],
				layout + " preserves the right input channel while expanding");
			for (unsigned channel = 2; channel < 8; ++channel)
				harness.expectEqual(output[frame * 8 + channel], 0.0f,
					layout + " silences an added output channel");
		}
	};

	float output[frames * 8];
	std::fill_n(output, frames * 8, -1.0f);
	engine.process(output, input, frames);
	expectExpanded(output, "distinct-buffer empty config");

	float inPlace[frames * 8];
	std::fill_n(inPlace, frames * 8, -1.0f);
	std::copy_n(input, frames * 2, inPlace);
	engine.process(inPlace, inPlace, frames);
	expectExpanded(inPlace, "in-place empty config");
}

// Audit #422 E2/TD-01: the seed and loaded configurations use one channel
// count rule. Both asymmetric shapes accepted by the APO must survive their
// first block, when the pending initial load still reads and writes the seed.
void testSeedConfigurationSupportsAsymmetricChannels(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"empty-asymmetric.txt",
		"# All filters are disabled\n");

	auto processShape = [&](bool capture, unsigned realChannels,
		unsigned outputChannels, const char* label) {
		FilterEngine engine;
		EngineSetup setup;
		setup.inputChannelCount = realChannels;
		setup.realChannelCount = realChannels;
		setup.outputChannelCount = outputChannels;
		setup.maxFrameCount = 16;
		setup.customPath = configPath;
		setup.capture = capture;
		setup.deviceName = L"EngineOrchestrationTests";
		setup.connectionName = L"File";
		engine.initialize(setup);

		constexpr unsigned frames = 4;
		std::vector<float> input(static_cast<size_t>(frames) * realChannels);
		std::vector<float> output(static_cast<size_t>(frames) * outputChannels, -1.0f);
		for (unsigned frame = 0; frame < frames; ++frame)
			for (unsigned channel = 0; channel < realChannels; ++channel)
				input[static_cast<size_t>(frame) * realChannels + channel]
					= static_cast<float>(10 * frame + channel + 1);
		engine.process(output.data(), input.data(), frames);

		for (unsigned frame = 0; frame < frames; ++frame)
		{
			for (unsigned channel = 0; channel < outputChannels; ++channel)
			{
				const bool monoUpmix = realChannels == 1 && outputChannels >= 2
					&& channel == 1;
				const float expected = channel < realChannels || monoUpmix
					? input[static_cast<size_t>(frame) * realChannels]
					: 0.0f;
				harness.expectEqual(
					output[static_cast<size_t>(frame) * outputChannels + channel],
					expected, label);
			}
		}
	};

	processShape(false, 2, 1,
		"render 2-in/1-out seed preserves the output channel");
	processShape(true, 1, 2,
		"capture 1-in/2-out seed preserves the engine's mono-to-stereo upmix");
}

// Loading a new config while processing must crossfade smoothly from the old
// configuration to the new one (transitionLength = sampleRate / 100 samples),
// not step. Drives DC through a -6.0206 dB config, swaps to -20 dB, and
// checks continuity, bounds, progress, and convergence.
void testConfigSwapCrossfades(test::Harness& harness)
{
	const unsigned sampleRate = 48000;
	const unsigned blockFrames = 120;

	std::wstring configA = writeConfig(harness, L"transition_a.txt", "Preamp: -6.0206 dB\n");
	std::wstring configB = writeConfig(harness, L"transition_b.txt", "Preamp: -20 dB\n");

	FilterEngine engine;
	initializeEngine(engine, sampleRate, 2, 480, configA);

	// The engine's real crossfade length; do not re-derive its formula here.
	// A zero length would leave the sampled transition empty, so the midpoint
	// read below needs this as a gating check.
	const unsigned transitionLength = engine.getTransitionLength();
	harness.require(transitionLength > 0, "engine reported no transition length after initialize");

	// Settle on config A.
	std::vector<float> settled = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(settled[0] - 0.5f) < 1e-3f, "engine did not settle on the initial config");

	// loadConfig with an existing currentConfig installs nextConfig and arms
	// the transition; process() then plays the crossfade.
	engine.loadConfig(configB);

	const float target = std::pow(10.0f, -20.0f / 20.0f); // 0.1
	std::vector<float> transition;
	for (unsigned block = 0; block * blockFrames < transitionLength; block++)
	{
		std::vector<float> output = processDcBlock(engine, 1.0f, 1.0f, blockFrames);
		for (unsigned i = 0; i < blockFrames; i++)
			transition.push_back(output[(size_t)i * 2]);
	}

	// Continuity: with a raised-cosine table over 480 samples and a 0.4 gain
	// span, adjacent samples may differ by ~0.0013; 0.01 leaves wide margin
	// while still failing hard on a step change.
	float previous = 0.5f;
	float maxDelta = 0.0f;
	for (float sample : transition)
	{
		maxDelta = std::max(maxDelta, std::fabs(sample - previous));
		previous = sample;
		harness.expect(sample <= 0.5f + 1e-3f && sample >= target - 1e-3f,
			"transition output left the [new gain, old gain] range");
	}
	harness.expect(maxDelta < 0.01f, "transition stepped instead of crossfading");

	// Progress: halfway through the transition the gain must sit strictly
	// between the two configs.
	float midway = transition[transition.size() / 2];
	harness.expect(midway < 0.49f && midway > 0.11f, "transition did not progress between the two configs");

	// Convergence: after the transition the new config is in sole control.
	std::vector<float> after = processDcBlock(engine, 1.0f, 1.0f, 480);
	float finalLeft = after[(size_t)478 * 2 + 0];
	float finalRight = after[(size_t)478 * 2 + 1];
	harness.expect(std::fabs(finalLeft - target) < 1e-4f, "left channel did not converge to the new config gain");
	harness.expect(std::fabs(finalRight - target) < 1e-4f, "right channel did not converge to the new config gain");
}

// A reload is one transaction: filters may have been constructed and
// initialized, but the active configuration must not change unless the final
// FilterConfiguration allocation also succeeds. This injects failure at that
// exact allocation (one PreampFilter allocation succeeds first).
void testFailedConfigLoadKeepsActiveConfiguration(test::Harness& harness)
{
	std::wstring configA = writeConfig(harness, L"transaction_a.txt", "Preamp: -6.0206 dB\n");
	std::wstring configB = writeConfig(harness, L"transaction_b.txt", "Preamp: -20 dB\n");

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, configA);
	std::vector<float> before = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(before[0] - 0.5f) < 1e-3f, "transaction test did not establish config A");

	AlignedMemory::failAllocationAfterForTesting(1);
	bool loaded = engine.loadConfig(configB);
	AlignedMemory::resetAllocationFailureForTesting();
	harness.expectFalse(loaded, "reload reported success after injected FilterConfiguration allocation failure");

	std::vector<float> after = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(after[0] - 0.5f) < 1e-3f,
		"failed reload replaced or damaged the active configuration");
}

void testConfigWatcherBackoffAndPathRefresh(test::Harness& harness)
{
	const std::wstring firstDirectory = testDirectory() + L"\\watch-a";
	const std::wstring secondDirectory = testDirectory() + L"\\watch-b";
	CreateDirectoryW(firstDirectory.c_str(), nullptr);
	CreateDirectoryW(secondDirectory.c_str(), nullptr);

	std::atomic<int> selectedPath = 0;
	std::atomic<int> snapshotCount = 0;
	std::atomic<int> callbackCount = 0;
	Win32Event shutdown(true, false);
	Win32Event changed(true, false);
	ConfigWatcher watcher(
		shutdown.get(),
		[&] {
			++snapshotCount;
			ConfigWatcher::Snapshot snapshot;
			if (selectedPath == 0)
				snapshot.directory = testDirectory() + L"\\missing-watch";
			else if (selectedPath == 1)
				snapshot.directory = firstDirectory;
			else
				snapshot.directory = secondDirectory;
			return snapshot;
		},
		[&] {
			++callbackCount;
			changed.set();
			return true;
		},
		20);
	std::thread worker([&] { watcher.run(); });

	const bool observedBackoffTicks = waitForCondition(
		[&] {return snapshotCount.load() >= 3;}, std::chrono::milliseconds(500));
	harness.expect(observedBackoffTicks,
		"unavailable config watch keeps retrying at the configured interval");
	// Audit #250 F054: the bound catches a hot loop rather than pinning a wall
	// clock delay. Reaching three snapshot passes is the synchronization point.
	harness.expect(snapshotCount.load() < 50,
		"unavailable config watch uses backoff instead of hot-looping");

	auto triggerAndWait = [&](int pathIndex, const std::wstring& directory,
		const char* label) {
		selectedPath = pathIndex;
		changed.reset();
		bool observed = false;
		for (int attempt = 0; attempt < 24 && !observed; ++attempt)
		{
			const std::wstring file = directory + L"\\change-"
				+ std::to_wstring(attempt) + L".txt";
			HANDLE output = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (output != INVALID_HANDLE_VALUE)
				CloseHandle(output);
			observed = WaitForSingleObject(changed.get(), 150) == WAIT_OBJECT_0;
			DeleteFileW(file.c_str());
		}
		harness.expect(observed, label);
	};

	triggerAndWait(1, firstDirectory,
		"watcher recovers when the config directory becomes available");
	triggerAndWait(2, secondDirectory,
		"watcher follows a ConfigPath directory change");

	shutdown.set();
	worker.join();
	RemoveDirectoryW(firstDirectory.c_str());
	RemoveDirectoryW(secondDirectory.c_str());
	harness.expect(callbackCount.load() >= 2,
		"both config directories produced change callbacks");
}

// Audit #348 TD-01: a configuration that reads the registry (readRegString,
// readRegDWORD) makes the watcher arm RegNotifyChangeKeyValue on that key.
// The audit read that closing an armed key signals the event and concluded
// the watcher reloads forever; closing does signal it, but re-arming resets
// it (measured on Windows 11 22621), so it never looped. This pins the
// property either way: no reload without a change, exactly one per change.
// The key lives under the HKCU test sandbox and is removed on every path.
void testConfigWatcherRegistryKeyIsQuietUntilChanged(test::Harness& harness)
{
	GUID guid = {};
	CoCreateGuid(&guid);
	wchar_t guidText[64] = {};
	StringFromGUID2(guid, guidText, 64);
	const std::wstring subKey = std::wstring(L"Software\\EqualizerAPO-XT-Tests\\") + guidText;
	const std::wstring fullKey = L"HKEY_CURRENT_USER\\" + subKey;

	HKEY created = nullptr;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0,
		KEY_SET_VALUE, nullptr, &created, nullptr) != ERROR_SUCCESS)
	{
		harness.expect(false, "could not create the HKCU sandbox key for the registry watch test");
		return;
	}
	RegCloseKey(created);

	std::atomic<int> callbackCount = 0;
	std::atomic<int> snapshotCount = 0;
	Win32Event shutdown(true, false);
	Win32Event changed(true, false);
	ConfigWatcher watcher(
		shutdown.get(),
		[&] {
			++snapshotCount;
			ConfigWatcher::Snapshot snapshot;
			snapshot.registryKeys.push_back(fullKey);
			return snapshot;
		},
		[&] {
			++callbackCount;
			changed.set();
			return true;
		},
		20);
	std::thread worker([&] { watcher.run(); });

	// Three retry passes with no change must stay silent.
	const bool quietPassesCompleted = waitForCondition(
		[&] {return snapshotCount.load() >= 3;}, std::chrono::milliseconds(500));
	harness.expect(quietPassesCompleted,
		"the registry watcher completes three quiet retry passes");
	harness.expect(callbackCount.load() == 0,
		"a watched registry key without changes does not trigger reloads");

	changed.reset();
	const DWORD value = 1;
	RegSetKeyValueW(HKEY_CURRENT_USER, subKey.c_str(), L"Probe", REG_DWORD,
		&value, sizeof(value));
	const bool observed = WaitForSingleObject(changed.get(), 2000) == WAIT_OBJECT_0;
	const int snapshotsAfterChange = snapshotCount.load();
	const bool rearmedPassesCompleted = waitForCondition(
		[&] {return snapshotCount.load() >= snapshotsAfterChange + 3;},
		std::chrono::milliseconds(500));
	harness.expect(observed, "a registry value change triggers a reload");
	harness.expect(rearmedPassesCompleted,
		"the registry watcher stays armed for three passes after the change");
	harness.expect(callbackCount.load() == 1,
		"one registry value change triggers exactly one reload");

	shutdown.set();
	worker.join();
	RegDeleteTreeW(HKEY_CURRENT_USER, subKey.c_str());
	RegDeleteKeyW(HKEY_CURRENT_USER, subKey.c_str());
	// Fails harmlessly while another run's sandbox key still exists.
	RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\EqualizerAPO-XT-Tests");
}
