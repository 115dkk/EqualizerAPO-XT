/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include "engine/ConfigLoadTrace.h"
#include "filters/NewChannelCommand.h"
#include "services/logging/Logging.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
struct TraceCollector : ConfigLoadTraceSink
{
	std::vector<ConfigLoadTraceEntry> entries;

	void addEntry(const ConfigLoadTraceEntry& entry) override
	{
		entries.push_back(entry);
	}
};

std::vector<float> processConstantBlock(FilterEngine& engine, unsigned channels,
	const std::vector<float>& values, unsigned frames)
{
	std::vector<float> input(static_cast<size_t>(frames) * channels);
	std::vector<float> output(static_cast<size_t>(frames) * channels, 0.0f);
	for (unsigned frame = 0; frame < frames; frame++)
	{
		for (unsigned channel = 0; channel < channels; channel++)
			input[static_cast<size_t>(frame) * channels + channel] = values[channel];
	}
	engine.process(output.data(), input.data(), frames);
	engine.process(output.data(), input.data(), frames);
	return output;
}

std::wstring readLog(FILE* stream)
{
	std::fflush(stream);
	std::rewind(stream);
	std::wstring result;
	wchar_t buffer[1024];
	while (std::fgetws(buffer, static_cast<int>(std::size(buffer)), stream) != nullptr)
		result.append(buffer);
	return result;
}

size_t countText(const std::wstring& text, const std::wstring& needle)
{
	size_t count = 0;
	size_t position = 0;
	while ((position = text.find(needle, position)) != std::wstring::npos)
	{
		count++;
		position += needle.size();
	}
	return count;
}

void assertParser(test::Harness& harness)
{
	NewChannelCommand command;
	harness.require(NewChannelCommand::parse(L"NewChannel", L" vc, Vrl\tvc,VRR  ", command),
		"NewChannel parser accepts its exact command");
	harness.expect(command.names == std::vector<std::wstring>({L"VC", L"VRL", L"VRR"}),
		"NewChannel parser splits commas and whitespace, upper-cases, and deduplicates");
	harness.expect(command.serialize() == L"VC VRL VRR",
		"NewChannel serializer joins canonical names with one space");

	NewChannelCommand roundTrip;
	harness.require(NewChannelCommand::parse(L"NewChannel", command.serialize(), roundTrip),
		"serialized NewChannel parameters parse again");
	harness.expect(roundTrip.names == command.names,
		"NewChannel parse and serialize round-trip preserves names");
	harness.expectFalse(NewChannelCommand::parse(L"Newchannel", L"VC", roundTrip),
		"NewChannel command matching is case-sensitive");

	NewChannelCommand empty;
	harness.expect(NewChannelCommand::parse(L"NewChannel", L" , \t", empty) && empty.names.empty(),
		"NewChannel parser accepts an empty token list for the factory to diagnose");
}

void assertRemixAndSilence(test::Harness& harness)
{
	const std::wstring remixPath = writeConfig(harness, L"newchannel-remix.txt",
		"NewChannel: VC\n"
		"Copy: VC=0.5*L+0.5*R\n"
		"Copy: L=VC R=VC\n");
	FilterEngine remix;
	initializeEngine(remix, 48000, 2, 480, remixPath);
	const std::vector<float> remixed = processConstantBlock(remix, 2, {0.75f, 0.25f}, 480);
	const size_t last = static_cast<size_t>(479) * 2;
	harness.expectNear(remixed[last], 0.5, 1.0e-6,
		"a virtual channel can hold a stereo remix copied into L");
	harness.expectNear(remixed[last + 1], 0.5, 1.0e-6,
		"a virtual channel can hold a stereo remix copied into R");

	const std::wstring silentPath = writeConfig(harness, L"newchannel-silent.txt",
		"NewChannel: VC\n"
		"Copy: L=VC\n");
	FilterEngine silent;
	initializeEngine(silent, 48000, 2, 480, silentPath);
	const std::vector<float> silence = processConstantBlock(silent, 2, {0.75f, 0.25f}, 480);
	harness.expectNear(silence[last], 0.0, 1.0e-9,
		"a virtual channel is silent immediately after declaration");
	harness.expectNear(silence[last + 1], 0.25, 1.0e-6,
		"copying a silent virtual channel to L leaves R untouched");
}

void assertSelectionTrace(test::Harness& harness)
{
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr,
		"selection trace capture is available");
	Logging::useStream(logFile, true, true, false);

	const std::wstring selectedPath = writeConfig(harness, L"newchannel-selection.txt",
		"Channel: L\n"
		"NewChannel: VC\n"
		"Preamp: -6.0206 dB\n");
	FilterEngine selected;
	initializeEngine(selected, 48000, 2, 480, selectedPath);
	const std::vector<float> selectedOutput = processConstantBlock(selected, 2, {1.0f, 1.0f}, 480);

	const std::wstring allPath = writeConfig(harness, L"newchannel-selection-all.txt",
		"NewChannel: VC\n"
		"Preamp: -6.0206 dB\n");
	FilterEngine all;
	initializeEngine(all, 48000, 2, 480, allPath);
	const std::vector<float> allOutput = processConstantBlock(all, 2, {1.0f, 1.0f}, 480);

	const std::wstring log = readLog(logFile);
	std::fclose(logFile);
	Logging::useStream(stderr, false, false, false);

	const size_t last = static_cast<size_t>(479) * 2;
	harness.expectNear(selectedOutput[last], 0.5, 1.0e-3,
		"NewChannel preserves the previous L selection for the following preamp");
	harness.expectNear(selectedOutput[last + 1], 1.0, 1.0e-6,
		"NewChannel does not add an unselected device channel");
	harness.expectNear(allOutput[last], 0.5, 1.0e-3,
		"without Channel, NewChannel keeps L selected");
	harness.expectNear(allOutput[last + 1], 0.5, 1.0e-3,
		"without Channel, NewChannel keeps R selected");
	harness.expect(log.find(L"selection is now L VC") != std::wstring::npos,
		"the trace reports previous selection plus the new name");
	harness.expect(log.find(L"selection is now L R VC") != std::wstring::npos,
		"the trace reports all device channels plus the new name by default");
}

void assertDuplicateDeclarations(test::Harness& harness)
{
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr,
		"duplicate trace capture is available");
	Logging::useStream(logFile, true, true, false);

	const std::wstring repeatedPath = writeConfig(harness, L"newchannel-duplicates.txt",
		"NewChannel: VC\n"
		"NewChannel: VC\n"
		"Copy: L=VC\n");
	FilterEngine repeated;
	initializeEngine(repeated, 48000, 2, 480, repeatedPath);
	const std::vector<float> repeatedOutput = processConstantBlock(repeated, 2, {1.0f, 1.0f}, 480);

	const std::wstring copyFirstPath = writeConfig(harness, L"newchannel-copy-first.txt",
		"Channel: L\n"
		"Copy: VC=0\n"
		"NewChannel: VC\n"
		"Copy: L=VC\n");
	FilterEngine copyFirst;
	initializeEngine(copyFirst, 48000, 2, 480, copyFirstPath);
	const std::vector<float> copyFirstOutput = processConstantBlock(copyFirst, 2, {1.0f, 1.0f}, 480);

	const std::wstring log = readLog(logFile);
	std::fclose(logFile);
	Logging::useStream(stderr, false, false, false);

	const size_t last = static_cast<size_t>(479) * 2;
	harness.expectNear(repeatedOutput[last], 0.0, 1.0e-9,
		"declaring the same virtual channel twice keeps one silent channel");
	harness.expectNear(copyFirstOutput[last], 0.0, 1.0e-9,
		"redeclaring a virtual channel after Copy keeps one usable channel");
	harness.expectNear(copyFirstOutput[last + 1], 1.0, 1.0e-6,
		"duplicate declarations do not disturb an unselected device channel");
	harness.expectEqual(countText(log, L"Already declared, added to the selection only: VC"), size_t(2),
		"both a repeated NewChannel and a channel first declared by Copy are traced as already declared");
	harness.expect(log.find(L"selection is now L R VC VC") == std::wstring::npos,
		"duplicate declaration never duplicates the selection");
}

void assertRejectedNames(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"newchannel-rejections.txt",
		"Channel: L\nNewChannel: 1X\nNewChannel: VD\n"
		"Channel: L\nNewChannel: all\nNewChannel: VD\n"
		"Channel: L\nNewChannel: L\nNewChannel: VD\n"
		"Channel: L\nNewChannel: SL\nNewChannel: VD\n"
		"Channel: L\nNewChannel: SUB\nNewChannel: VD\n"
		"Channel: L\nNewChannel: A=B\nNewChannel: VD\n"
		"Channel: L\nNewChannel: VC 2B\nNewChannel: VD\n"
		"Channel: L\nNewChannel:\nNewChannel: VD\n"
		"Channel: L\nNewChannel: VC\n"
		"Preamp: -6.0206 dB\n");

	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr,
		"rejection trace capture is available");
	Logging::useStream(logFile, true, true, false);
	TraceCollector collector;
	FilterEngine engine;
	engine.setLoadTraceSink(&collector);
	EngineSetup setup = testEngineSetup(48000, 6, 6, 480, configPath);
	// 5.1 with back channels names them RL/RR, so SL is rejected through the
	// ChannelLayout alias rule rather than by an exact-name match.
	setup.channelMask = 0x0000003F;
	engine.initialize(setup);
	const std::vector<float> output = processConstantBlock(engine, 6,
		{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}, 480);
	const std::wstring log = readLog(logFile);
	std::fclose(logFile);
	Logging::useStream(stderr, false, false, false);

	std::vector<const ConfigLoadTraceEntry*> errors;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind == ConfigLoadTraceEntry::Kind::ParseError)
			errors.push_back(&entry);
	}
	const std::vector<std::wstring> expected = {
		L"1X (starts with a digit)",
		L"ALL (ALL is reserved)",
		L"L (is a device channel)",
		L"SL (is a device channel)",
		L"SUB (is a device channel)",
		L"A=B (contains a character Copy uses as a separator)",
		L"2B (starts with a digit)",
		L"expected at least one channel name"
	};
	harness.requireEqual(errors.size(), expected.size(),
		"each invalid NewChannel line reports one parse error");
	for (size_t index = 0; index < expected.size(); index++)
	{
		harness.expect(errors[index]->text.find(expected[index]) != std::wstring::npos,
			"the NewChannel parse error names the rejected token and reason");
	}

	harness.expectEqual(countText(log, L"selection is now L VD"), expected.size(),
		"every rejected line leaves selection unchanged and keeps all names from that line out");
	harness.expect(log.find(L"Already declared, added to the selection only: VC") == std::wstring::npos,
		"a line with one invalid token does not partially declare its valid token");
	const size_t last = static_cast<size_t>(479) * 6;
	harness.expectNear(output[last], 0.5, 1.0e-3,
		"the unchanged L selection reaches the following preamp");
	for (size_t channel = 1; channel < 6; channel++)
		harness.expectNear(output[last + channel], 1.0, 1.0e-6,
			"a rejected NewChannel line does not select another device channel");
}

void assertLowerCaseSelection(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"newchannel-lowercase.txt",
		"NewChannel: vc\n"
		"Copy: VC=L\n"
		"Channel: VC\n"
		"Preamp: -6.0206 dB\n"
		"Channel: vc\n"
		"Preamp: -6.0206 dB\n"
		"Copy: L=VC\n");
	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, configPath);
	const std::vector<float> output = processConstantBlock(engine, 2, {1.0f, 0.75f}, 480);
	const size_t last = static_cast<size_t>(479) * 2;
	harness.expectNear(output[last], 0.25, 1.0e-3,
		"lower-case declaration is upper-cased and both Channel spellings select it");
	harness.expectNear(output[last + 1], 0.75, 1.0e-6,
		"selecting a lower-case virtual channel leaves R untouched");
}
}

void runNewChannelTests(test::Harness& harness)
{
	assertParser(harness);
	assertRemixAndSilence(harness);
	assertSelectionTrace(harness);
	assertDuplicateDeclarations(harness);
	assertRejectedNames(harness);
	assertLowerCaseSelection(harness);
}
