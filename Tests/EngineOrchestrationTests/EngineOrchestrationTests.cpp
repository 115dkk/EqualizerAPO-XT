/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Engine orchestration tests. Exercises the parts of FilterEngine and its
	surroundings that the audio regression suite cannot localize: channel-name
	resolution into filter in/out channel indices, Copy routing, the crossfade
	transition state machine that runs when a new configuration is loaded while
	audio is processing, the load trace, the device and install vocabulary,
	and the registry and file-system boundaries. All configs are written to a
	temp directory at runtime, so the tests carry no data files and are
	deterministic.

	This file holds the fixtures every source of the executable shares
	(declared in EngineOrchestrationTestSupport.h) and main(). The tests live
	in topic files and register themselves with TestHarness.
*/

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "services/logging/Logging.h"
#include "Tests/AlignedMemoryGate.h"
#include "Tests/TestDirectory.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
test::TestDirectory& testDirectoryFixture()
{
	static test::TestDirectory directory(L"EngineOrchestrationTests");
	return directory;
}
}

std::wstring testDirectory()
{
	return testDirectoryFixture().path();
}

std::wstring testSubdirectory(const std::wstring& name)
{
	return testDirectoryFixture().subdirectory(name);
}

void trackWrittenFile(const std::wstring& path)
{
	testDirectoryFixture().track(path);
}

void trackTestSubdirectory(const std::wstring& path)
{
	testDirectoryFixture().trackDirectory(path);
}

void removeTestDirectory()
{
	testDirectoryFixture().removeAll();
}

std::wstring writeConfig(const test::Harness& harness, const std::wstring& fileName, const std::string& content)
{
	const std::wstring path = testDirectoryFixture().trackFile(fileName);
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream << content;
	stream.close();
	if (!stream)
		harness.fail("could not write temp config file");
	return path;
}

EngineSetup testEngineSetup(unsigned sampleRate, unsigned inputChannels, unsigned outputChannels,
	unsigned maxFrameCount, const std::wstring& configPath)
{
	EngineSetup setup;
	setup.sampleRate = (float)sampleRate;
	setup.inputChannelCount = inputChannels;
	setup.realChannelCount = inputChannels;
	setup.outputChannelCount = outputChannels;
	setup.maxFrameCount = maxFrameCount;
	setup.customPath = configPath;
	setup.deviceName = L"EngineOrchestrationTests";
	setup.connectionName = L"File";
	setup.deviceGuid = L"";
	return setup;
}

void initializeEngine(FilterEngine& engine, unsigned sampleRate, unsigned channels, unsigned maxFrameCount,
	const std::wstring& configPath)
{
	engine.initialize(testEngineSetup(sampleRate, channels, channels, maxFrameCount, configPath));
}

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

// Temp paths and config text are ASCII in these tests; this avoids the
// wchar_t->char narrowing warning (C4244) that the std::string(begin, end)
// shortcut raises.
std::string toNarrow(const std::wstring& w)
{
	if (w.empty())
		return std::string();
	int len = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)len, '\0');
	WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), &s[0], len, nullptr, nullptr);
	return s;
}

int main()
{
	Logging::set(stderr, false, false, false);
	try
	{
		return test::runAll("EngineOrchestrationTests", {}, removeTestDirectory);
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "EngineOrchestrationTests: unhandled exception: %s\n", error.what());
	}
	catch (...)
	{
		std::fprintf(stderr, "EngineOrchestrationTests: unhandled non-standard exception\n");
	}
	return EXIT_FAILURE;
}
