/*
	This file is part of EqualizerAPO-XT.

	Engine orchestration tests. Exercises the parts of FilterEngine that the
	audio regression suite cannot localize: channel-name resolution into
	filter in/out channel indices, Copy routing, and the crossfade transition
	state machine that runs when a new configuration is loaded while audio is
	processing. All configs are written to a temp directory at runtime, so the
	tests carry no data files and are deterministic.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "FilterEngine.h"
#include "helpers/LogHelper.h"
#include "Tests/TestHarness.h"

namespace
{

std::wstring testDirectory()
{
	wchar_t tempPath[MAX_PATH] = {};
	DWORD len = GetTempPathW(MAX_PATH, tempPath);
	std::wstring dir = (len > 0 && len < MAX_PATH) ? tempPath : L".";
	dir += L"EngineOrchestrationTests";
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

std::wstring writeConfig(test::Harness& harness, const std::wstring& fileName, const std::string& content)
{
	std::wstring path = testDirectory() + L"\\" + fileName;
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream << content;
	stream.close();
	if (!stream)
		harness.fail("could not write temp config file");
	return path;
}

// Builds an engine the same way AudioRegressionTests does: no registry
// dependency, config loaded from the caller-supplied path.
void initializeEngine(FilterEngine& engine, unsigned sampleRate, unsigned channels, unsigned maxFrameCount, const std::wstring& configPath)
{
	const std::wstring deviceName = L"EngineOrchestrationTests";
	const std::wstring connectionName = L"File";
	const std::wstring deviceGuid = L"";
	engine.setDeviceInfo(false, true, deviceName, connectionName, deviceGuid, deviceName + L" " + connectionName);
	engine.initialize((float)sampleRate, channels, channels, channels, 0, maxFrameCount, configPath);
}

// Processes one block of interleaved stereo DC input and returns the output.
std::vector<float> processDcBlock(FilterEngine& engine, float left, float right, unsigned frames)
{
	std::vector<float> input((size_t)frames * 2);
	std::vector<float> output((size_t)frames * 2, 0.0f);
	for (unsigned i = 0; i < frames; i++)
	{
		input[(size_t)i * 2 + 0] = left;
		input[(size_t)i * 2 + 1] = right;
	}
	engine.process(output.data(), input.data(), frames);
	return output;
}

// A Channel selector must route the following filter to the named channel
// only. This pins down the channel-name -> channel-index resolution in
// FilterEngine::addFilters.
void testChannelSelectorRouting(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"channel_selector.txt",
			"Channel: L\n"
			"Preamp: -6.0206 dB\n");

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, config);

	std::vector<float> output = processDcBlock(engine, 1.0f, 1.0f, 480);

	// -6.0206 dB is a gain of 10^(-6.0206/20) ~= 0.49999
	float left = output[(size_t)478 * 2 + 0];
	float right = output[(size_t)478 * 2 + 1];
	harness.expect(std::fabs(left - 0.5f) < 1e-3f, "left channel was not attenuated by the selected preamp");
	harness.expect(right == 1.0f, "right channel was modified although only L was selected");
}

// Copy assignments read from the input snapshot, so a simultaneous swap must
// not see partially written data.
void testCopySwapsChannels(test::Harness& harness)
{
	std::wstring config = writeConfig(harness, L"copy_swap.txt",
			"Copy: L=R R=L\n");

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, config);

	std::vector<float> output = processDcBlock(engine, 0.75f, 0.25f, 480);

	float left = output[(size_t)478 * 2 + 0];
	float right = output[(size_t)478 * 2 + 1];
	harness.expect(std::fabs(left - 0.25f) < 1e-6f, "Copy did not route R into L");
	harness.expect(std::fabs(right - 0.75f) < 1e-6f, "Copy did not route L into R");
}

// Loading a new config while processing must crossfade smoothly from the old
// configuration to the new one (transitionLength = sampleRate / 100 samples),
// not step. Drives DC through a -6.0206 dB config, swaps to -20 dB, and
// checks continuity, bounds, progress, and convergence.
void testConfigSwapCrossfades(test::Harness& harness)
{
	const unsigned sampleRate = 48000;
	const unsigned transitionLength = sampleRate / 100; // matches FilterEngine::initialize
	const unsigned blockFrames = 120;

	std::wstring configA = writeConfig(harness, L"transition_a.txt", "Preamp: -6.0206 dB\n");
	std::wstring configB = writeConfig(harness, L"transition_b.txt", "Preamp: -20 dB\n");

	FilterEngine engine;
	initializeEngine(engine, sampleRate, 2, 480, configA);

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

} // namespace

int main()
{
	LogHelper::set(stderr, false, false, false);

	test::Harness harness("EngineOrchestrationTests");

	testChannelSelectorRouting(harness);
	testCopySwapsChannels(harness);
	testConfigSwapCrossfades(harness);

	harness.report();
	return 0;
}
