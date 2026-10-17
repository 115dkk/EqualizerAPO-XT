/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the EngineOrchestrationTests sources share: the per-process
	temporary directory, the one config writer and the engine set-up the
	file-driven tests use. The fixtures are defined in
	EngineOrchestrationTests.cpp; each test self-registers from its topic file.
*/

#pragma once

#include <string>
#include <vector>

#include "engine/FilterEngine.h"
#include "../TestHarness.h"

inline test::HarnessAccess harness;

// The per-process temporary directory (Tests/TestDirectory.h) every test in
// this executable writes into. main() removes it and every tracked file at
// the end.
std::wstring testDirectory();
std::wstring testSubdirectory(const std::wstring& name);
// Registers a file or specially-created directory under testDirectory() for
// removal at the end.
void trackWrittenFile(const std::wstring& path);
void trackTestSubdirectory(const std::wstring& path);
void removeTestDirectory();

// Writes content to testDirectory()\fileName, registers it for removal and
// returns its path. A failed write aborts the suite.
std::wstring writeConfig(const test::Harness& harness, const std::wstring& fileName, const std::string& content);

// The EngineSetup the file-driven tests share: no registry dependency, the
// config loaded from configPath, device "EngineOrchestrationTests" on
// connection "File". Callers change the fields their case is about.
EngineSetup testEngineSetup(unsigned sampleRate, unsigned inputChannels, unsigned outputChannels,
	unsigned maxFrameCount, const std::wstring& configPath);
// Builds an engine the same way AudioRegressionTests does, from
// testEngineSetup with as many output channels as input channels.
void initializeEngine(FilterEngine& engine, unsigned sampleRate, unsigned channels, unsigned maxFrameCount,
	const std::wstring& configPath);
// Processes one block of interleaved stereo DC input and returns the output.
std::vector<float> processDcBlock(FilterEngine& engine, float left, float right, unsigned frames);
// Narrows a wide string to the active code page, for paths spliced into
// config text.
std::string toNarrow(const std::wstring& w);
