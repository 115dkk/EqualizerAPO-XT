/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Unit tests for the multi-input synthesis convolution filter
	(MultiConvolutionFilter): each mapping target's own signal convolved with
	the listed channels of a single multi-channel impulse response and summed
	back into that target, independent of the Channel command's selection.
*/

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "filters/ConvolutionFilter.h"
#include "filters/MultiConvolutionCommand.h"
#include "filters/MultiConvolutionFilter.h"
#include "audio/io/SndfileRAII.h"
#include "services/logging/Logging.h"
#include "diagnostics/performance/PerfProfile.h"
#include "Tests/TestHarness.h"

using std::vector;
using std::wstring;

namespace
{
using IrRef = MultiConvolutionCommand::IrChannelRef;

constexpr int frameLength = 480;
constexpr int sampleRate = 48000;
constexpr double tolerance = 1.0e-8;

test::Harness harness("MultiConvolutionTests");

// Writes a multi-channel impulse response to a temporary WAV. channels[c] holds
// the samples of IR channel c; all channels must have the same length.
wstring createMultiChannelIr(const vector<vector<double>>& channels)
{
	const unsigned numCh = (unsigned)channels.size();
	const unsigned frames = (unsigned)channels[0].size();
	vector<double> interleaved((size_t)frames * numCh);
	for (unsigned f = 0; f < frames; f++)
		for (unsigned c = 0; c < numCh; c++)
			interleaved[(size_t)f * numCh + c] = channels[c][f];

	wchar_t tempPath[MAX_PATH] = {};
	wchar_t tempFile[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0)
		harness.fail("GetTempPathW failed");
	if (GetTempFileNameW(tempPath, L"mc", 0, tempFile) == 0)
		harness.fail("GetTempFileNameW failed");

	wstring filename = tempFile;
	DeleteFileW(filename.c_str());
	filename += L".wav";

	SF_INFO info = {};
	info.samplerate = sampleRate;
	info.channels = (int)numCh;
	info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;

	sndfile::Handle file(sf_wchar_open(filename.c_str(), SFM_WRITE, &info));
	if (!file)
		harness.fail("could not create temporary impulse response file");
	sf_writef_double(file.get(), interleaved.data(), (sf_count_t)frames);

	return filename;
}

void assertMismatchIsLoggedAndProfiled()
{
	FILE* logFile = nullptr;
	if (tmpfile_s(&logFile) != 0 || logFile == nullptr)
	{
		harness.fail("could not create mismatch log capture");
		return;
	}
	Logging::useStream(logFile, false, true, false);
	PerfProfile::reset();
	PerfProfile::enable();

	{
		vector<double> ir(frameLength, 0.0);
		ir[0] = 1.0;
		wstring irFile = createMultiChannelIr({ ir });
		MultiConvolutionFilter filter({ { L"L", { 0 } } }, irFile);
		filter.initialize((float)sampleRate, frameLength, vector<wstring>{ L"L" });
		DeleteFileW(irFile.c_str());

		constexpr unsigned shortBlock = frameLength / 2;
		vector<double> in(shortBlock, 0.1);
		vector<double> out(shortBlock, 1.0);
		double* input[] = { in.data() };
		double* output[] = { out.data() };
		filter.process(output, input, shortBlock);
		filter.process(output, input, shortBlock);
	}

	PerfProfile::disable();
	std::ostringstream profile;
	PerfProfile::report(profile);
	harness.expectTrue(profile.str().find("MultiConvolutionFilter::process") != std::string::npos,
		"MultiConvolution process contributes a profiling scope");

	std::fflush(logFile);
	std::rewind(logFile);
	std::wstring log;
	wchar_t buffer[1024];
	while (std::fgetws(buffer, static_cast<int>(std::size(buffer)), logFile) != nullptr)
		log.append(buffer);
	std::fclose(logFile);
	Logging::useStream(stdout, true, true, false);

	// Audit #250 F058t: the marker is the filter's own constant, so a
	// wording edit cannot silently detach this assertion from the code.
	const std::wstring marker = MultiConvolutionFilter::kFrameCountMismatchLogPrefix;
	const size_t first = log.find(marker);
	harness.expectTrue(first != std::string::npos,
		"MultiConvolution frame-count mismatch is logged");
	harness.expectTrue(first != std::string::npos && log.find(marker, first + marker.size()) == std::string::npos,
		"MultiConvolution mismatch detail is logged only once per instance");
}

// The Convolution sibling of the assertion above, colocated to reuse the IR
// fixture and log-capture pattern (audit #250 A4: one shared diagnostics type
// now covers all three convolvers, and each filter's prefix constant is its
// observable contract).
void assertConvolutionMismatchIsLogged()
{
	FILE* logFile = nullptr;
	if (tmpfile_s(&logFile) != 0 || logFile == nullptr)
	{
		harness.fail("could not create convolution mismatch log capture");
		return;
	}
	Logging::useStream(logFile, false, true, false);

	{
		vector<double> ir(frameLength, 0.0);
		ir[0] = 1.0;
		wstring irFile = createMultiChannelIr({ ir });
		ConvolutionFilter filter(irFile);
		filter.initialize((float)sampleRate, frameLength, vector<wstring>{ L"L" });
		DeleteFileW(irFile.c_str());

		constexpr unsigned shortBlock = frameLength / 2;
		vector<double> in(shortBlock, 0.1);
		vector<double> out(shortBlock, 1.0);
		double* input[] = { in.data() };
		double* output[] = { out.data() };
		filter.process(output, input, shortBlock);
		filter.process(output, input, shortBlock);
		harness.expectTrue(out[0] == 0.0, "a mismatched Convolution block is muted");
	}

	std::fflush(logFile);
	std::rewind(logFile);
	std::wstring log;
	wchar_t buffer[1024];
	while (std::fgetws(buffer, static_cast<int>(std::size(buffer)), logFile) != nullptr)
		log.append(buffer);
	std::fclose(logFile);
	Logging::useStream(stdout, true, true, false);

	const std::wstring marker = ConvolutionFilter::kFrameCountMismatchLogPrefix;
	const size_t first = log.find(marker);
	harness.expectTrue(first != std::string::npos,
		"Convolution frame-count mismatch is logged");
	harness.expectTrue(first != std::string::npos && log.find(marker, first + marker.size()) == std::string::npos,
		"Convolution mismatch detail is logged only once per instance");
}

// First tracer bullet for the mapping semantics: "L=0+1" convolves channel L's
// OWN pre-command signal with IR channels 0 (2x) and 1 (3x) and sums into L,
// giving 0.1*(2+3) = 0.5. The other channel (R = 0.7) must not take part; the
// old selection-based pairing would have produced 0.1*2 + 0.7*3 = 2.3, so this
// value discriminates the two semantics.
void assertMappingConvolvesTargetsOwnSignal()
{
	vector<double> ir0(frameLength, 0.0);
	ir0[0] = 2.0;
	vector<double> ir1(frameLength, 0.0);
	ir1[0] = 3.0;
	wstring irFile = createMultiChannelIr({ir0, ir1});

	MultiConvolutionFilter filter({{L"L", {0, 1}}}, irFile);
	vector<wstring> allChannels = {L"L", L"R"};
	vector<wstring> outChannels = filter.initialize((float)sampleRate, frameLength, allChannels);
	DeleteFileW(irFile.c_str());

	harness.expectTrue(filter.getAllChannels(), "the filter asks for every channel instead of the selection");
	// process() writes output[plan.outputSlot] for every mapping, so a wider
	// declaration than the output array below would run off its end.
	harness.requireEqual(outChannels.size(), (size_t)1, "one mapping declares one output channel");
	harness.expectTrue(outChannels[0] == L"L", "the mapping target is the output channel");

	vector<double> inL(frameLength, 0.1);
	vector<double> inR(frameLength, 0.7);
	vector<double> out(frameLength, 0.0);
	double* input[] = {inL.data(), inR.data()};
	double* output[] = {out.data()};
	filter.process(output, input, frameLength);

	for (int f = 0; f < frameLength; f++)
		harness.expectTrue(fabs(out[f] - 0.5) <= tolerance, "a mapping convolves the target's own signal with its listed IR channels");
}

// Two mappings write two independent outputs: "L=0 R=1" gives L = 0.1*2 and
// R = 0.7*3. Each target reads its own pre-command signal and only its listed
// IR channel.
void assertEachMappingWritesItsOwnOutput()
{
	vector<double> ir0(frameLength, 0.0);
	ir0[0] = 2.0;
	vector<double> ir1(frameLength, 0.0);
	ir1[0] = 3.0;
	wstring irFile = createMultiChannelIr({ir0, ir1});

	MultiConvolutionFilter filter({{L"L", {0}}, {L"R", {1}}}, irFile);
	vector<wstring> allChannels = {L"L", L"R"};
	vector<wstring> outChannels = filter.initialize((float)sampleRate, frameLength, allChannels);
	DeleteFileW(irFile.c_str());

	harness.requireEqual(outChannels.size(), (size_t)2, "two mappings declare two output channels");

	vector<double> inL(frameLength, 0.1);
	vector<double> inR(frameLength, 0.7);
	vector<double> outL(frameLength, 0.0);
	vector<double> outR(frameLength, 0.0);
	double* input[] = {inL.data(), inR.data()};
	double* output[] = {outL.data(), outR.data()};
	filter.process(output, input, frameLength);

	for (int f = 0; f < frameLength; f++)
	{
		harness.expectTrue(fabs(outL[f] - 0.2) <= tolerance, "the first mapping feeds only its own target");
		harness.expectTrue(fabs(outR[f] - 2.1) <= tolerance, "the second mapping feeds only its own target");
	}
}

// The simple form (empty IR channel list) means every channel of the file:
// with a 2-channel IR (2x, 3x) the target L = 0.1 becomes 0.1*(2+3) = 0.5,
// using only L's own signal. This is the released one-liner's meaning without
// the Channel command.
void assertSimpleFormUsesEveryIrChannel()
{
	vector<double> ir0(frameLength, 0.0);
	ir0[0] = 2.0;
	vector<double> ir1(frameLength, 0.0);
	ir1[0] = 3.0;
	wstring irFile = createMultiChannelIr({ir0, ir1});

	MultiConvolutionFilter filter({{L"L", {}}}, irFile);
	vector<wstring> allChannels = {L"L", L"R"};
	vector<wstring> outChannels = filter.initialize((float)sampleRate, frameLength, allChannels);
	DeleteFileW(irFile.c_str());

	harness.requireEqual(outChannels.size(), (size_t)1, "simple form declares one output channel");

	vector<double> inL(frameLength, 0.1);
	vector<double> inR(frameLength, 0.7);
	vector<double> out(frameLength, 0.0);
	double* input[] = {inL.data(), inR.data()};
	double* output[] = {out.data()};
	filter.process(output, input, frameLength);

	for (int f = 0; f < frameLength; f++)
		harness.expectTrue(fabs(out[f] - 0.5) <= tolerance, "the simple form sums every IR channel over the target's own signal");
}

// A target that does not exist yet is declared as a new (virtual) output
// channel but reads silence, and an IR channel reference beyond the file's
// channel count contributes silence instead of failing the line. A duplicated
// target behaves like Copy: the later mapping overwrites the earlier one.
void assertMissingSourcesAndDuplicatesDegradeGracefully()
{
	vector<double> ir0(frameLength, 0.0);
	ir0[0] = 2.0;
	vector<double> ir1(frameLength, 0.0);
	ir1[0] = 3.0;

	{
		wstring irFile = createMultiChannelIr({ir0, ir1});
		MultiConvolutionFilter filter({{L"Wet", {0}}}, irFile);
		vector<wstring> allChannels = {L"L", L"R"};
		vector<wstring> outChannels = filter.initialize((float)sampleRate, frameLength, allChannels);
		DeleteFileW(irFile.c_str());

		harness.requireEqual(outChannels.size(), (size_t)1, "a new target is declared as an output channel");
		harness.expectTrue(outChannels[0] == L"Wet", "the virtual output keeps its name");

		vector<double> inL(frameLength, 0.1);
		vector<double> inR(frameLength, 0.7);
		vector<double> out(frameLength, 123.0);
		double* input[] = {inL.data(), inR.data()};
		double* output[] = {out.data()};
		filter.process(output, input, frameLength);

		for (int f = 0; f < frameLength; f++)
			harness.expectTrue(out[f] == 0.0, "a fresh virtual target reads silence but is still written");
	}

	{
		wstring irFile = createMultiChannelIr({ir0, ir1});
		MultiConvolutionFilter filter({{L"L", {0, 7}}}, irFile);
		vector<wstring> allChannels = {L"L", L"R"};
		filter.initialize((float)sampleRate, frameLength, allChannels);
		DeleteFileW(irFile.c_str());

		vector<double> inL(frameLength, 0.1);
		vector<double> inR(frameLength, 0.7);
		vector<double> out(frameLength, 0.0);
		double* input[] = {inL.data(), inR.data()};
		double* output[] = {out.data()};
		filter.process(output, input, frameLength);

		for (int f = 0; f < frameLength; f++)
			harness.expectTrue(fabs(out[f] - 0.2) <= tolerance, "an out-of-range IR channel contributes silence");
	}

	{
		wstring irFile = createMultiChannelIr({ir0, ir1});
		MultiConvolutionFilter filter({{L"L", {0}}, {L"L", {1}}}, irFile);
		vector<wstring> allChannels = {L"L", L"R"};
		vector<wstring> outChannels = filter.initialize((float)sampleRate, frameLength, allChannels);
		DeleteFileW(irFile.c_str());

		harness.requireEqual(outChannels.size(), (size_t)1, "duplicate targets share one output channel");

		vector<double> inL(frameLength, 0.1);
		vector<double> inR(frameLength, 0.7);
		vector<double> out(frameLength, 0.0);
		double* input[] = {inL.data(), inR.data()};
		double* output[] = {out.data()};
		filter.process(output, input, frameLength);

		for (int f = 0; f < frameLength; f++)
			harness.expectTrue(fabs(out[f] - 0.3) <= tolerance, "the later duplicate mapping overwrites the earlier one");
	}
}

// The simple form "<target> <path>" parses into one mapping with an empty IR
// channel list (= every channel of the file). The channel is the first token;
// the rest (trimmed) is the path and keeps its inner spaces. A non-matching
// command or a line without a path is rejected.
void assertCommandParsesChannelAndPath()
{
	MultiConvolutionCommand cmd;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"Mixed room.wav", cmd), "valid MultiConvolution line parses");
	harness.expectEqual(cmd.mappings.size(), (size_t)1, "simple form carries one mapping");
	harness.expectTrue(!cmd.mappings.empty() && cmd.mappings[0].targetChannel == L"Mixed", "output channel is the first token");
	harness.expectTrue(!cmd.mappings.empty() && cmd.mappings[0].irChannels.empty(), "simple form means every IR channel");
	harness.expectTrue(cmd.isSimpleForm(), "simple form is recognized");
	harness.expectTrue(cmd.path == L"room.wav", "IR path is the remainder");

	MultiConvolutionCommand spaced;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"LeftEar sub dir\\room ir.wav", spaced), "path with inner spaces parses");
	harness.expectTrue(!spaced.mappings.empty() && spaced.mappings[0].targetChannel == L"LeftEar", "channel stops at the first space");
	harness.expectTrue(spaced.path == L"sub dir\\room ir.wav", "path keeps inner spaces");

	MultiConvolutionCommand rejected;
	harness.expectFalse(MultiConvolutionCommand::parse(L"Convolution", L"Mixed room.wav", rejected), "a non-MultiConvolution command is rejected");
	harness.expectFalse(MultiConvolutionCommand::parse(L"MultiConvolution", L"OnlyChannel", rejected), "a line without an IR path is rejected");
}

// The mapping form assigns explicit 0-based IR channels to each output channel:
// "L=0+1 R=2+3 brir.wav". Whitespace around '=' and '+' must not change the
// result, and serialize() writes the compact canonical text back.
void assertMappingGrammarParses()
{
	MultiConvolutionCommand compact;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=0+1 R=2+3 brir.wav", compact), "compact mapping form parses");
	harness.expectEqual(compact.mappings.size(), (size_t)2, "two mappings parse");
	harness.expectTrue(compact.mappings.size() == 2 && compact.mappings[0].targetChannel == L"L" && compact.mappings[1].targetChannel == L"R", "targets keep their order");
	harness.expectTrue(compact.mappings.size() == 2 && compact.mappings[0].irChannels == std::vector<IrRef>({0, 1}), "first mapping lists IR channels 0 and 1");
	harness.expectTrue(compact.mappings.size() == 2 && compact.mappings[1].irChannels == std::vector<IrRef>({2, 3}), "second mapping lists IR channels 2 and 3");
	harness.expectTrue(compact.path == L"brir.wav", "path follows the mappings");
	harness.expectFalse(compact.isSimpleForm(), "mapping form is not the simple form");

	MultiConvolutionCommand spaced;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L = 0 + 1 R = 2 + 3 brir.wav", spaced), "spaced mapping form parses");
	harness.expectTrue(spaced.mappings.size() == 2
		&& spaced.mappings[0].targetChannel == L"L" && spaced.mappings[0].irChannels == std::vector<IrRef>({0, 1})
		&& spaced.mappings[1].targetChannel == L"R" && spaced.mappings[1].irChannels == std::vector<IrRef>({2, 3})
		&& spaced.path == L"brir.wav", "spacing around '=' and '+' does not change the parse");

	harness.expectTrue(compact.serialize() == L"L=0+1 R=2+3 brir.wav", "serialization is the compact form");
	harness.expectTrue(spaced.serialize() == L"L=0+1 R=2+3 brir.wav", "spaced input serializes to the same compact text");

	MultiConvolutionCommand reparsed;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", compact.serialize(), reparsed), "serialized mapping line re-parses");
	harness.expectTrue(reparsed.mappings.size() == 2
		&& reparsed.mappings[0].targetChannel == L"L" && reparsed.mappings[0].irChannels == std::vector<IrRef>({0, 1})
		&& reparsed.mappings[1].targetChannel == L"R" && reparsed.mappings[1].irChannels == std::vector<IrRef>({2, 3})
		&& reparsed.path == L"brir.wav", "mapping form round-trips");
}

// The path is the raw remainder after the last complete mapping, so file names
// containing '=', '+', leading digits or inner spaces survive, and a mapping
// line without any path is rejected.
void assertPathBoundaryIsRobust()
{
	MultiConvolutionCommand equalsPath;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=0+1 a=b.wav", equalsPath), "path containing '=' parses");
	harness.expectTrue(equalsPath.mappings.size() == 1 && equalsPath.mappings[0].irChannels == std::vector<IrRef>({0, 1}), "mapping before '=' path is kept");
	harness.expectTrue(equalsPath.path == L"a=b.wav", "word failing the mapping grammar starts the path");

	MultiConvolutionCommand plusPath;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=0 mix+mono.wav", plusPath), "path containing '+' parses");
	// A parenthesized single-element brace ("({0})") would pick the vector's
	// size constructor over the initializer list (0 -> IrRef is a user-defined
	// conversion), so single elements are spelled out.
	harness.expectTrue(plusPath.mappings.size() == 1 && plusPath.mappings[0].irChannels == std::vector<IrRef>{IrRef(0)}, "single IR channel mapping parses");
	harness.expectTrue(plusPath.path == L"mix+mono.wav", "path with '+' is not cut apart");

	MultiConvolutionCommand digitPath;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"Ear=3 2ch room ir.wav", digitPath), "digit-leading path parses");
	harness.expectTrue(digitPath.mappings.size() == 1 && digitPath.mappings[0].targetChannel == L"Ear"
		&& digitPath.mappings[0].irChannels == std::vector<IrRef>{IrRef(3)}, "single mapping with one IR channel parses");
	harness.expectTrue(digitPath.path == L"2ch room ir.wav", "digit-leading path keeps inner spaces");

	MultiConvolutionCommand starPath;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=0 2*ch.wav", starPath), "path containing '*' parses");
	harness.expectTrue(starPath.mappings.size() == 1 && starPath.mappings[0].irChannels == std::vector<IrRef>{IrRef(0)}
		&& starPath.path == L"2*ch.wav", "a word that is not a valid summand starts the path even with a '*'");

	MultiConvolutionCommand simpleEquals;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"Mixed a=b.wav", simpleEquals), "simple form with '=' in the path parses");
	harness.expectTrue(simpleEquals.mappings.size() == 1 && simpleEquals.mappings[0].targetChannel == L"Mixed"
		&& simpleEquals.mappings[0].irChannels.empty(), "simple form target is kept");
	harness.expectTrue(simpleEquals.path == L"a=b.wav", "simple form path keeps the '='");

	MultiConvolutionCommand rejected;
	harness.expectFalse(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=0+1", rejected), "a mapping line without a path is rejected");
	harness.expectFalse(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=0+1 R=2+3", rejected), "several mappings without a path are rejected");
}

// The factor grammar mirrors Copy: "<factor>*<ir ch>" scales that IR channel's
// convolution result, a negative factor inverts the phase, and a dB suffix
// keeps its raw dB value with the isDecibel marker. Serialization writes the
// factor back only when it is not unity, and the whole line round-trips.
void assertFactorGrammarParses()
{
	MultiConvolutionCommand factored;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=0.5*0+1 R=-1*2+-0.5*3 brir.wav", factored), "factored mapping form parses");
	harness.expectTrue(factored.mappings.size() == 2
		&& factored.mappings[0].irChannels == std::vector<IrRef>({IrRef(0, 0.5), IrRef(1)})
		&& factored.mappings[1].irChannels == std::vector<IrRef>({IrRef(2, -1.0), IrRef(3, -0.5)})
		&& factored.path == L"brir.wav", "factors, inversion and inverted low factors parse");
	harness.expectTrue(factored.serialize() == L"L=0.5*0+1 R=-1*2+-0.5*3 brir.wav", "factors serialize compactly and unity factors stay bare");

	MultiConvolutionCommand spaced;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L = 0.5*0 + 1 brir.wav", spaced), "spaced factored form parses");
	harness.expectTrue(spaced.mappings.size() == 1
		&& spaced.mappings[0].irChannels == std::vector<IrRef>({IrRef(0, 0.5), IrRef(1)}), "spacing does not change the factored parse");

	MultiConvolutionCommand decibel;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"L=-6dB*0 brir.wav", decibel), "dB factor parses");
	harness.expectTrue(decibel.mappings.size() == 1
		&& decibel.mappings[0].irChannels == std::vector<IrRef>({IrRef(0, -6.0, true)}), "the dB factor keeps its raw value and marker");
	harness.expectTrue(decibel.serialize() == L"L=-6dB*0 brir.wav", "the dB factor round-trips");

	MultiConvolutionCommand reparsed;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", factored.serialize(), reparsed), "serialized factored line re-parses");
	harness.expectTrue(reparsed.mappings.size() == 2
		&& reparsed.mappings[0].irChannels == factored.mappings[0].irChannels
		&& reparsed.mappings[1].irChannels == factored.mappings[1].irChannels, "factored mappings survive the round trip");

	// An invalid factor ("x*0", "0.5**1") is not a summand, so the word starts
	// the path exactly like any other non-mapping word.
	MultiConvolutionCommand invalid;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"Mixed x*0 y.wav", invalid), "a non-numeric factor word falls back to the path");
	harness.expectTrue(invalid.mappings.size() == 1 && invalid.mappings[0].targetChannel == L"Mixed"
		&& invalid.path == L"x*0 y.wav", "the invalid summand word starts the path");
}

// The filter applies each summand's factor to that IR channel's convolution
// result before the sum: 0.5 halves, -1 inverts, -0.5 does both, and a dB
// factor converts like Copy (pow(10, dB/20)).
void assertFactorScalesConvolutionResult()
{
	vector<double> ir0(frameLength, 0.0);
	ir0[0] = 2.0;
	vector<double> ir1(frameLength, 0.0);
	ir1[0] = 3.0;

	struct Case
	{
		IrRef ref0;
		bool alsoUnity1 = false;
		double expected = 0.0;
		const char* label = "";
	};
	const Case cases[] = {
		{IrRef(0, 0.5), true, 0.5 * 0.2 + 0.3, "a 0.5 factor halves its IR channel's share of the sum"},
		{IrRef(0, -1.0), false, -0.2, "a -1 factor inverts the phase"},
		{IrRef(0, -0.5), false, -0.1, "a -0.5 factor inverts and attenuates"},
		{IrRef(0, -6.0, true), false, pow(10.0, -6.0 / 20.0) * 0.2, "a dB factor converts to its linear scale"},
	};

	for (const Case& c : cases)
	{
		vector<IrRef> refs = {c.ref0};
		if (c.alsoUnity1)
			refs.push_back(IrRef(1));
		wstring irFile = createMultiChannelIr({ir0, ir1});
		MultiConvolutionFilter filter({{L"L", refs}}, irFile);
		vector<wstring> allChannels = {L"L", L"R"};
		filter.initialize((float)sampleRate, frameLength, allChannels);
		DeleteFileW(irFile.c_str());

		vector<double> inL(frameLength, 0.1);
		vector<double> inR(frameLength, 0.7);
		vector<double> out(frameLength, 0.0);
		double* input[] = {inL.data(), inR.data()};
		double* output[] = {out.data()};
		filter.process(output, input, frameLength);

		for (int f = 0; f < frameLength; f++)
			harness.expectTrue(fabs(out[f] - c.expected) <= tolerance, c.label);
	}
}

// serialize -> parse round trip is stable, so the Editor can write the line and
// read it back unchanged (the card and the engine share this one codec).
void assertCommandSerializeRoundTrips()
{
	struct Case
	{
		const wchar_t* channel;
		const wchar_t* path;
	};
	const Case cases[] = {
		{L"Mixed", L"room.wav"},
		{L"LeftEar", L"sub dir\\room ir.wav"},
		{L"R", L"%USERPROFILE%\\ir.wav"},
	};

	for (const Case& c : cases)
	{
		MultiConvolutionCommand first;
		first.mappings.push_back({c.channel, {}});
		first.path = c.path;
		wstring serialized = first.serialize();

		MultiConvolutionCommand second;
		harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", serialized, second), "serialized line re-parses");
		harness.expectTrue(second.mappings.size() == 1 && second.mappings[0].targetChannel == c.channel, "channel round-trips");
		harness.expectTrue(second.mappings.size() == 1 && second.mappings[0].irChannels.empty(), "simple form round-trips as simple form");
		harness.expectTrue(second.path == first.path, "path round-trips");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}
} // namespace

void runMultiConvolutionTests()
{
	assertMismatchIsLoggedAndProfiled();
	assertConvolutionMismatchIsLogged();
	assertMappingConvolvesTargetsOwnSignal();
	assertEachMappingWritesItsOwnOutput();
	assertSimpleFormUsesEveryIrChannel();
	assertMissingSourcesAndDuplicatesDegradeGracefully();
	assertCommandParsesChannelAndPath();
	assertMappingGrammarParses();
	assertPathBoundaryIsRobust();
	assertFactorGrammarParses();
	assertFactorScalesConvolutionResult();
	assertCommandSerializeRoundTrips();
	harness.report();
}
