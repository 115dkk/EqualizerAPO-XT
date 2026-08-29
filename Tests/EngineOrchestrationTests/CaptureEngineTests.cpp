/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The engine's capture contract, pinned where the APO cannot be run: a
	capture endpoint carries one APO (the pre-mix slot), so the engine is
	initialized with capture=true, preMix=true and postMixInstalled=false.
	Every line of a configuration that names no stage must then apply exactly
	once - Configuration reference, "Stage": the initial selection is post-mix
	and capture - and the Stage/Device/If vocabulary must resolve the way the
	documentation promises for an input device. A field report said neither
	the EQ nor a VST reached a recording device; these tests separate "the
	engine skips capture" (which they would catch) from "the APO never ran"
	(which the capture gate in CI covers).
*/

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "engine/FilterEngine.h"
#include "Tests/TestDirectory.h"
#include "Tests/TestHarness.h"

namespace
{
test::TestDirectory& captureDirectory()
{
	static test::TestDirectory directory(L"EngineOrchestrationTests-capture");
	return directory;
}

std::wstring writeConfig(test::Harness& harness, const std::wstring& fileName, const std::string& content)
{
	const std::wstring path = captureDirectory().trackFile(fileName);
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream << content;
	stream.close();
	if (!stream)
		harness.fail("could not write temp config file");
	return path;
}

// The APO's own assembly for a capture endpoint: EqualizerAPO::Initialize
// sets preMix from the CLSID and capture/postMixInstalled from the device
// record, LockForProcess supplies the stream facts.
void initializeCaptureEngine(FilterEngine& engine, unsigned inputChannels, unsigned outputChannels,
	const std::wstring& configPath, bool preMix = true)
{
	EngineSetup setup;
	setup.sampleRate = 48000.0f;
	setup.inputChannelCount = inputChannels;
	setup.realChannelCount = inputChannels;
	setup.outputChannelCount = outputChannels;
	setup.maxFrameCount = 480;
	setup.customPath = configPath;
	setup.preMix = preMix;
	setup.capture = true;
	setup.postMixInstalled = false;
	setup.deviceName = L"VB-Audio Virtual Cable";
	setup.connectionName = L"CABLE Output";
	setup.deviceGuid = L"{209b12ab-81a3-4eed-a4a4-dbb0781fba92}";
	engine.initialize(setup);
}

// One block of DC at `level` on every channel; returns the last frame's
// channel values. The initial load skips the crossfade (transitionCounter is
// seeded to the transition length), so any frame of the first block is the
// settled output.
std::vector<float> processDc(FilterEngine& engine, unsigned inputChannels, unsigned outputChannels, float level)
{
	const unsigned frames = 480;
	std::vector<float> input((size_t)frames * inputChannels, level);
	std::vector<float> output((size_t)frames * outputChannels, 0.0f);
	engine.process(output.data(), input.data(), frames);
	std::vector<float> last(outputChannels);
	for (unsigned c = 0; c < outputChannels; c++)
		last[c] = output[(size_t)(frames - 1) * outputChannels + c];
	return last;
}

bool closeTo(float value, float expected)
{
	return std::fabs(value - expected) < 1e-3f;
}

// -6.0206 dB is a factor of 0.5; two of them are 0.25.
const char* const halfPreamp = "Preamp: -6.0206 dB\n";

// A configuration that names no stage applies to a capture endpoint - once.
void testUnstagedLinesApplyOnCapture(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"capture_unstaged.txt", halfPreamp);
	FilterEngine engine;
	initializeCaptureEngine(engine, 2, 2, config);
	harness.expectTrue(engine.isCapture(), "the engine reports the capture endpoint it was set up for");
	harness.expectFalse(engine.isPostMixInstalled(), "a capture endpoint has no post-mix APO");

	std::vector<float> out = processDc(engine, 2, 2, 1.0f);
	harness.expect(closeTo(out[0], 0.5f) && closeTo(out[1], 0.5f),
		"a preamp with no Stage line must reach the capture stream (initial stages are post-mix and capture)");
}

// Stage: capture selects, Stage: pre-mix and Stage: post-mix deselect, and
// a set naming capture among others selects again.
void testStageSelectionOnCapture(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"capture_stages.txt",
			"Stage: post-mix\n"
			"Preamp: -20 dB\n"
			"Stage: pre-mix\n"
			"Preamp: -20 dB\n"
			"Stage: capture\n"
			"Preamp: -6.0206 dB\n"
			"Stage: pre-mix capture\n"
			"Preamp: -6.0206 dB\n");
	FilterEngine engine;
	initializeCaptureEngine(engine, 2, 2, config);

	std::vector<float> out = processDc(engine, 2, 2, 1.0f);
	harness.expect(closeTo(out[0], 0.25f),
		"only the two blocks that name the capture stage apply on a capture endpoint (post-mix and pre-mix blocks are skipped)");
}

// The parser constant the documentation lists: stage == "capture" on an
// input device, so If: blocks can tell the two directions apart.
void testStageConstantIsCapture(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"capture_if.txt",
			"If: stage == \"capture\"\n"
			"Preamp: -6.0206 dB\n"
			"EndIf:\n"
			"If: stage == \"post-mix\"\n"
			"Preamp: -20 dB\n"
			"EndIf:\n");
	FilterEngine engine;
	initializeCaptureEngine(engine, 2, 2, config);

	std::vector<float> out = processDc(engine, 2, 2, 1.0f);
	harness.expect(closeTo(out[0], 0.5f),
		"the stage constant reads \"capture\" on an input device and the post-mix branch stays out");
}

// Device: matches the capture endpoint's connection name, device name and
// GUID the same way it matches a playback endpoint.
void testDeviceMatchingOnCapture(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"capture_device.txt",
			"Device: CABLE Input VB-Audio Virtual Cable\n"
			"Preamp: -20 dB\n"
			"Device: CABLE Output\n"
			"Preamp: -6.0206 dB\n"
			"Device: {209b12ab-81a3-4eed-a4a4-dbb0781fba92}\n"
			"Preamp: -6.0206 dB\n"
			"Device: all\n"
			"Preamp: 0 dB\n");
	FilterEngine engine;
	initializeCaptureEngine(engine, 2, 2, config);

	std::vector<float> out = processDc(engine, 2, 2, 1.0f);
	harness.expect(closeTo(out[0], 0.25f),
		"Device: selects a capture endpoint by connection name and by GUID, and does not match the playback side of the same cable");
}

// A mono microphone: one input channel, one output channel, a channel mask
// the engine has to invent. The preamp must still land, and a Channel line
// naming the mono channel must resolve.
void testMonoCaptureEndpoint(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"capture_mono.txt", halfPreamp);
	FilterEngine engine;
	initializeCaptureEngine(engine, 1, 1, config);
	harness.expectEqual(engine.getInputChannelCount(), 1u, "mono capture keeps one input channel");
	harness.expect(engine.getChannelMask() != 0u, "a zero mask is replaced by the default layout for the channel count");

	std::vector<float> out = processDc(engine, 1, 1, 1.0f);
	harness.expect(closeTo(out[0], 0.5f), "the preamp reaches a mono capture stream");
}

// The APO's capture-side channel choice: the engine names channels after the
// INPUT count on capture (FilterEngine::initialize), because that is the
// device side of a capture stream. A 2-in/1-out capture stream (an app that
// asked for mono from a stereo microphone) must still process.
void testCaptureUsesInputChannelsForTheDevice(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"capture_2in1out.txt",
			"Channel: L\n"
			"Preamp: -6.0206 dB\n");
	FilterEngine engine;
	initializeCaptureEngine(engine, 2, 1, config);
	harness.expectEqual(engine.getRealChannelCount(), 2u, "the real channel count is the input side on capture");

	std::vector<float> out = processDc(engine, 2, 1, 1.0f);
	harness.expect(closeTo(out[0], 0.5f),
		"the first output channel carries the processed left input when the app takes fewer channels than the device");
}
}

void runCaptureEngineTests(test::Harness& harness)
{
	testUnstagedLinesApplyOnCapture(harness);
	testStageSelectionOnCapture(harness);
	testStageConstantIsCapture(harness);
	testDeviceMatchingOnCapture(harness);
	testMonoCaptureEndpoint(harness);
	testCaptureUsesInputChannelsForTheDevice(harness);
	captureDirectory().removeAll();
}
