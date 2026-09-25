/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	What the engine does to the samples of a configuration: Channel
	selection, Copy's snapshot semantics, MultiConvolution's mapping
	targets, and (with EAPO_XT_BRIR_DIR set) crossfeed through a real BRIR
	set.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <sndfile.h>

#include "audio/io/SndfileRAII.h"
#include "Tests/WavFixtures.h"

#include "EngineOrchestrationTestSupport.h"

namespace
{
// Writes a 2-channel delta impulse response (each channel passes its input
// straight through) to the temp dir so a MultiConvolution line has a real file
// to load. Registered for cleanup like the configs.
std::wstring writeStereoDeltaIr(test::Harness& harness, const std::wstring& fileName)
{
	const std::wstring path = testDirectory() + L"\\" + fileName;
	const int frames = 16;
	std::vector<double> interleaved((size_t)frames * 2, 0.0);
	interleaved[0] = 1.0; // channel 0, first sample
	interleaved[1] = 1.0; // channel 1, first sample
	trackWrittenFile(path);
	harness.require(test::writeWavFile(path, 48000, 2, interleaved), "the stereo delta IR is written completely");
	return path;
}

// Reads a stereo IR file into two channel-major buffers. Fails the harness if
// the file is missing or not stereo.
void readStereoIr(test::Harness& harness, const std::wstring& path, std::vector<double>& ch0, std::vector<double>& ch1)
{
	SF_INFO info = {};
	sndfile::Handle file(sf_wchar_open(path.c_str(), SFM_READ, &info));
	if (!file)
		harness.fail("could not open BRIR IR file");
	if (info.channels != 2)
		harness.fail("BRIR IR file is not stereo");
	std::vector<double> interleaved((size_t)info.frames * info.channels);
	sf_readf_double(file.get(), interleaved.data(), info.frames);
	ch0.resize((size_t)info.frames);
	ch1.resize((size_t)info.frames);
	for (sf_count_t i = 0; i < info.frames; i++)
	{
		ch0[(size_t)i] = interleaved[(size_t)i * 2 + 0];
		ch1[(size_t)i] = interleaved[(size_t)i * 2 + 1];
	}
}

// Writes two channel-major buffers as a stereo double WAV, registered for
// cleanup. The longer buffer is cut to the shorter one's length.
void writeStereoIr(test::Harness& harness, const std::wstring& path, std::vector<double> ch0, std::vector<double> ch1)
{
	const size_t frames = std::min(ch0.size(), ch1.size());
	ch0.resize(frames);
	ch1.resize(frames);
	trackWrittenFile(path);
	harness.require(test::writeWavFile(path, 48000, {ch0, ch1}), "the ear IR is written completely");
}

// Drives a single unit impulse on the LEFT input through the engine and returns
// the accumulated L and R outputs across enough blocks to capture the full IR.
void processLeftImpulse(FilterEngine& engine, unsigned frames, unsigned blocks, std::vector<float>& outL, std::vector<float>& outR)
{
	outL.clear();
	outR.clear();
	for (unsigned b = 0; b < blocks; b++)
	{
		std::vector<float> input((size_t)frames * 2, 0.0f);
		std::vector<float> output((size_t)frames * 2, 0.0f);
		if (b == 0)
			input[0] = 1.0f; // left channel, first sample
		engine.process(output.data(), input.data(), frames);
		for (unsigned i = 0; i < frames; i++)
		{
			outL.push_back(output[(size_t)i * 2 + 0]);
			outR.push_back(output[(size_t)i * 2 + 1]);
		}
	}
}

// Peak magnitude and its sample index in a signal.
void peakOf(const std::vector<float>& sig, double& peak, size_t& pos)
{
	peak = 0.0;
	pos = 0;
	for (size_t i = 0; i < sig.size(); i++)
	{
		double a = std::fabs((double)sig[i]);
		if (a > peak) { peak = a; pos = i; }
	}
}

double energyOf(const std::vector<float>& sig)
{
	double e = 0.0;
	for (float s : sig)
		e += (double)s * (double)s;
	return e;
}
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

// MultiConvolution must convolve the mapping target's OWN signal with its
// listed IR channels, independent of the Channel selection. The config selects
// only R before the line; under the old selection-based semantics that would
// change the result, under the mapping semantics it must not. With a stereo
// delta IR (pass-through) and L = 0.3, "L=0+1" folds L's own signal in twice
// (0.6), while R (not a mapping target) passes through unchanged.
void testMultiConvolutionIgnoresChannelSelection(test::Harness& harness)
{
	std::wstring irPath = writeStereoDeltaIr(harness, L"mc_delta.wav");
	std::string irNarrow = toNarrow(irPath);

	std::wstring config = writeConfig(harness, L"multiconv.txt",
			"Channel: R\n"
			"MultiConvolution: L=0+1 \"" + irNarrow + "\"\n");

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, config);

	std::vector<float> output = processDcBlock(engine, 0.3f, 0.5f, 480);
	float left = output[(size_t)478 * 2 + 0];
	float right = output[(size_t)478 * 2 + 1];
	harness.expect(std::fabs(left - 0.6f) < 1e-3f, "L must be its own signal convolved with both mapped IR channels");
	harness.expect(std::fabs(right - 0.5f) < 1e-3f, "R must pass through unchanged (it is not a mapping target)");
}

// Diagnostic: with a real BRIR set, MultiConvolution must produce genuine
// crossfeed (a left-only impulse reaches the RIGHT ear), whereas the 1:1
// ConvolutionFilter cannot (its right output stays silent for right-input = 0).
// Runs only when EAPO_XT_BRIR_DIR points at a folder holding Thead400FL.wav and
// Thead400FR.wav (both stereo, [left-ear, right-ear]); otherwise it skips so CI
// stays green without the data files.
void testRealBrirCrossfeed(test::Harness& harness)
{
	wchar_t* dirBuf = nullptr;
	size_t dirLen = 0;
	_wdupenv_s(&dirBuf, &dirLen, L"EAPO_XT_BRIR_DIR");
	std::unique_ptr<wchar_t, decltype(&std::free)> dirOwner(dirBuf, &std::free);
	if (!dirOwner)
	{
		std::printf("  [skip] testRealBrirCrossfeed: set EAPO_XT_BRIR_DIR to the BRIR folder to run it\n");
		return;
	}
	std::wstring dir(dirOwner.get());

	std::wstring flPath = dir + L"\\Thead400FL.wav";
	std::wstring frPath = dir + L"\\Thead400FR.wav";

	std::vector<double> flL, flR, frL, frR;
	readStereoIr(harness, flPath, flL, flR); // FL: ch0 = L-ear, ch1 = R-ear
	readStereoIr(harness, frPath, frL, frR); // FR: ch0 = L-ear, ch1 = R-ear

	// Ear-based IRs: Lear = both speakers -> left ear, Rear = both -> right ear.
	std::wstring lear = testDirectory() + L"\\Lear.wav";
	std::wstring rear = testDirectory() + L"\\Rear.wav";
	writeStereoIr(harness, lear, flL, frL);
	writeStereoIr(harness, rear, flR, frR);

	const unsigned frames = 480;
	const unsigned blocks = 80; // 38400 samples > IR length

	// Config A: full BRIR via the mapping form. Each mapping convolves its
	// target's own signal, so the speaker feeds are first copied into scratch
	// channels (SO/SE for the left ear, TO/TE for the right), convolved in
	// place against their ear IR channel, then summed into the real ears.
	std::string cfgA =
			"Copy: SO=L SE=R TO=L TE=R\n"
			"MultiConvolution: SO=0 SE=1 \"" + toNarrow(lear) + "\"\n"
			"MultiConvolution: TO=0 TE=1 \"" + toNarrow(rear) + "\"\n"
			"Copy: L=SO+SE R=TO+TE\n";
	std::wstring cfgAPath = writeConfig(harness, L"brir_multi.txt", cfgA);

	FilterEngine engineA;
	initializeEngine(engineA, 48000, 2, frames, cfgAPath);
	std::vector<float> aL, aR;
	processLeftImpulse(engineA, frames, blocks, aL, aR);

	double aLpeak, aRpeak; size_t aLpos, aRpos;
	peakOf(aL, aLpeak, aLpos);
	peakOf(aR, aRpeak, aRpos);
	double aRenergy = energyOf(aR);
	std::printf("  [BRIR] MultiConvolution  L-ear peak=%.4f@%zu  R-ear(crossfeed) peak=%.4f@%zu energy=%.5f\n",
			aLpeak, aLpos, aRpeak, aRpos, aRenergy);

	// Config B: the old 1:1 ConvolutionFilter on the same stereo FL IR.
	std::string cfgB =
			"Channel: L R\n"
			"Convolution: \"" + toNarrow(flPath) + "\"\n";
	std::wstring cfgBPath = writeConfig(harness, L"brir_1to1.txt", cfgB);

	FilterEngine engineB;
	initializeEngine(engineB, 48000, 2, frames, cfgBPath);
	std::vector<float> bL, bR;
	processLeftImpulse(engineB, frames, blocks, bL, bR);

	double bLpeak, bRpeak; size_t bLpos, bRpos;
	peakOf(bL, bLpeak, bLpos);
	peakOf(bR, bRpeak, bRpos);
	double bRenergy = energyOf(bR);
	std::printf("  [BRIR] 1:1 Convolution   L-ear peak=%.4f@%zu  R-ear peak=%.4f@%zu energy=%.5f\n",
			bLpeak, bLpos, bRpeak, bRpos, bRenergy);

	// MultiConvolution must deliver crossfeed to the right ear...
	harness.expect(aRenergy > 1e-6, "MultiConvolution BRIR produced no crossfeed to the right ear");
	// ...matching the left speaker's contralateral response (FL right-ear channel,
	// peak ~0.0188 around sample 184).
	harness.expect(std::fabs(aRpeak - 0.0188) < 5e-3, "right-ear crossfeed peak does not match the contralateral IR");
	harness.expect(aRpos > 120 && aRpos < 260, "right-ear crossfeed peak is not at the expected contralateral delay");
	// The 1:1 path leaves the right ear essentially silent (no crossfeed).
	harness.expect(bRenergy < 1e-6, "1:1 Convolution unexpectedly produced right-ear output");
	harness.expect(aRenergy > bRenergy * 1000.0, "MultiConvolution crossfeed is not dramatically larger than the 1:1 path");
}
