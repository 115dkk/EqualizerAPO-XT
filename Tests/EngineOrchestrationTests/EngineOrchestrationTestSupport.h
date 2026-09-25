/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the EngineOrchestrationTests sources share: the per-process
	temporary directory, the one config writer, the engine set-up the file
	driven tests use, and the test functions main() calls in order. The
	fixtures are defined in EngineOrchestrationTests.cpp; each test function
	lives in the topic file named next to its declaration.
*/

#pragma once

#include <string>
#include <vector>

#include "engine/FilterEngine.h"
#include "Tests/TestHarness.h"

// The per-process temporary directory (Tests/TestDirectory.h) every test in
// this executable writes into. main() removes it and every tracked file at
// the end.
std::wstring testDirectory();
// Registers a file written under testDirectory() for removal at the end.
void trackWrittenFile(const std::wstring& path);
void removeTestDirectory();

// Writes content to testDirectory()\fileName, registers it for removal and
// returns its path. A failed write aborts the suite.
std::wstring writeConfig(test::Harness& harness, const std::wstring& fileName, const std::string& content);

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

// RuntimeUtilityTests.cpp
void testComBoundaryMapsExceptions(test::Harness& harness);
void testLogHelperFileDestination(test::Harness& harness);
void testLogHelperUserDestination(test::Harness& harness);
void testRegistryExportHeaderPreservesQualifiedRoot(test::Harness& harness);
void testSynchronizedStateSerializesReplacement(test::Harness& harness);
void testParallelExecutor(test::Harness& harness);
void testWeakValueCacheKeepsEntriesExactlyAsLongAsSomeoneUsesThem(test::Harness& harness);

// JudgedPathTests.cpp
void testJudgedPathAttributesOnlyAncestor(test::Harness& harness);
void testJudgedPathLifetime(test::Harness& harness);

// DeviceVocabularyTests.cpp
void testDeviceApoRegistryVocabulary(test::Harness& harness);
void testDeviceTestPipeNameAllowList(test::Harness& harness);
void testInstallStateComparisonIgnoresPadding(test::Harness& harness);
void testVoicemeeterPrependInfosMapsEditionToOutputCount(test::Harness& harness);
void testVoicemeeterStripVocabulary(test::Harness& harness);
void testProcessSearchLeavesTheTokenAsItWas(test::Harness& harness);

// EngineLifecycleTests.cpp
void testProcessWithoutConfigurationDoesNotCrash(test::Harness& harness);
void testInitialLoadUsesPublicationChannel(test::Harness& harness);
void testConfigSwapChannelPermitRoundTrip(test::Harness& harness);
void testEmptyConfigurationExpandsRenderChannels(test::Harness& harness);
void testConfigSwapCrossfades(test::Harness& harness);
void testFailedConfigLoadKeepsActiveConfiguration(test::Harness& harness);
void testConfigWatcherBackoffAndPathRefresh(test::Harness& harness);
void testConfigWatcherRegistryKeyIsQuietUntilChanged(test::Harness& harness);

// EngineRoutingTests.cpp
void testChannelSelectorRouting(test::Harness& harness);
void testCopySwapsChannels(test::Harness& harness);
void testMultiConvolutionIgnoresChannelSelection(test::Harness& harness);
void testRealBrirCrossfeed(test::Harness& harness);

// ConfigLoadTests.cpp
void testConfigLoadTrace(test::Harness& harness);
void testParseErrorsAreReportedPerLineAndProseIsNot(test::Harness& harness);
void testFilterSetupFailureNamesItsLine(test::Harness& harness);
void testControlFlowAndStageMistakesAreReported(test::Harness& harness);
void testIncludeRecursionLimitIsReported(test::Harness& harness);
void testConfigReferencedRemotePathsAreRefused(test::Harness& harness);
void testIncludeTakesQuotesAndVariables(test::Harness& harness);
void testConfigRegistryReadsGoThroughThePort(test::Harness& harness);
void testAnalysisFreezesDynamicVelvetAndLabelsTheSnapshot(test::Harness& harness);

// One runner per file, each running its own tests in order.
void runSampleIoTests(test::Harness& harness);
void runApoFormatTests(test::Harness& harness);
void runConfigurationFileReaderTests(test::Harness& harness);
void runDeviceApoInfoTests(test::Harness& harness);
void runRegistryTransactionTests(test::Harness& harness);
void runNamedPipeSecurityTests(test::Harness& harness);
void runRegistryConformanceTests(test::Harness& harness);
void runDevicePlanTests(test::Harness& harness);
void runDeviceTestPlanTests(test::Harness& harness);
void runInstallDiagnosticsTests(test::Harness& harness);
void runApoRegistrationTests(test::Harness& harness);
void runChannelInheritanceTests(test::Harness& harness);
void runCaptureEngineTests(test::Harness& harness);
