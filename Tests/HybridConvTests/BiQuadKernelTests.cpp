/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	BiQuadFilter kernel-plan and kernel-equivalence tests.

	The plan contract exists because of a dispatch gap found in 2026-07: the
	SIMD group covers floor(channelCount / laneWidth) * laneWidth channels, so
	on 4-lane (AVX2) and 8-lane (AVX-512) targets a stereo stream fell through
	to the single-chain scalar kernel entirely, one latency-bound dependency
	chain per channel. The plan is a pure function of (channelCount,
	laneWidth), so the contract is checkable for every SIMD width regardless
	of what the test host can execute.
*/

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "filters/BiQuad.h"
#include "filters/BiQuadFilter.h"
#include "filters/BiQuadFilterFactory.h"
#include "filters/BiQuadKernelPlan.h"
#include "dsp/MxcsrGuard.h"
#include "Tests/TestHarness.h"

using std::vector;
using std::wstring;

namespace
{
test::Harness harness("BiQuadKernelTests");

void testBiQuadRejectsNonFiniteParameters()
{
	const wchar_t* cases[] = {
		L"ON PK Fc 1e999 Hz Gain 3 dB Q 1",
		L"ON PK Fc 1000 Hz Gain 1e999 dB Q 1",
		L"ON PK Fc 1000 Hz Gain 3 dB Q 1e999",
	};
	for (const wchar_t* text : cases)
	{
		std::wstring parameters(text);
		BiQuadCommand command;
		harness.expectFalse(BiQuadFilterFactory::parseCommand(L"Filter", parameters, command),
			"BiQuad rejects a non-finite numeric parameter");
	}
}

// Audit #348 TD-13: only finiteness was checked, so a zero or negative
// frequency, Q or bandwidth reached the coefficient formulas.
void testBiQuadRejectsNonPositiveParameters()
{
	const wchar_t* cases[] = {
		L"ON PK Fc 0 Hz Gain 3 dB Q 1",
		L"ON PK Fc -100 Hz Gain 3 dB Q 1",
		L"ON PK Fc 1000 Hz Gain 3 dB Q -1",
		L"ON PK Fc 1000 Hz Gain 3 dB BW Oct -1",
		L"ON LS -6 dB Fc 100 Hz Gain 3 dB",
	};
	for (const wchar_t* text : cases)
	{
		std::wstring parameters(text);
		BiQuadCommand command;
		std::wstring error;
		harness.expectFalse(BiQuadFilterFactory::parseCommand(L"Filter", parameters, command, &error),
			"BiQuad rejects a non-positive frequency, Q, bandwidth or slope");
		harness.expectFalse(error.empty(), "the rejection carries a reason for the trace");
	}

	std::wstring valid(L"ON PK Fc 1000 Hz Gain 3 dB Q 1");
	BiQuadCommand command;
	harness.expectTrue(BiQuadFilterFactory::parseCommand(L"Filter", valid, command),
		"a positive peaking band still parses");
}

// A band exactly at Nyquist puts both poles on the unit circle; the section
// must pass the signal through instead of ringing forever. A band above
// Nyquist folds back and stays stable, as it always has, and must stay finite.
void testBiQuadAtNyquistPassesThrough()
{
	constexpr unsigned length = 4096;
	for (BiQuad::Type type : {BiQuad::PEAKING, BiQuad::LOW_PASS})
	{
		BiQuadFilter filter(type, 6.0, 22050.0, 0.7, false, false);
		vector<wstring> names(1, L"L");
		filter.initialize(44100.0f, length, names);

		vector<double> in(length, 0.0), out(length, 0.0);
		in[0] = 1.0;
		double* inPtr[1] = {in.data()};
		double* outPtr[1] = {out.data()};
		filter.process(outPtr, inPtr, length);
		harness.expect(memcmp(in.data(), out.data(), length * sizeof(double)) == 0,
			"a biquad exactly at Nyquist passes an impulse through unchanged");
	}

	BiQuadFilter folded(BiQuad::PEAKING, 6.0, 30000.0, 1.0, false, false);
	vector<wstring> names(1, L"L");
	folded.initialize(44100.0f, length, names);
	vector<double> in(length, 0.0), out(length, 0.0);
	in[0] = 1.0;
	double* inPtr[1] = {in.data()};
	double* outPtr[1] = {out.data()};
	folded.process(outPtr, inPtr, length);
	bool finite = true;
	for (double sample : out)
		finite = finite && std::isfinite(sample) && std::abs(sample) < 16.0;
	harness.expect(finite, "a biquad above Nyquist stays finite and bounded");
}

constexpr unsigned frameCount = 480;
constexpr unsigned blockCount = 3;

// Deterministic input; xorshift64* mapped into [-1, 1].
double nextSample(unsigned long long& state)
{
	state ^= state >> 12;
	state ^= state << 25;
	state ^= state >> 27;
	unsigned long long scrambled = state * 2685821657736338717ULL;
	return static_cast<double>(scrambled >> 11) / static_cast<double>(1ULL << 53) * 2.0 - 1.0;
}

// Every channel is served exactly once, in SIMD -> paired -> single order,
// and the SIMD group keeps its historical assignment: the SIMD kernel uses
// FMA where the scalar kernels do not, so moving a channel across that
// boundary changes the variant's audio regression references.
void testPlanCoversEveryChannelOnce()
{
	for (unsigned width : {2u, 4u, 8u})
	{
		for (unsigned channelCount = 1; channelCount <= 16; channelCount++)
		{
			const BiQuadKernelPlan plan = planBiQuadKernels(channelCount, width);
			char label[64];
			snprintf(label, sizeof(label), "ch=%u width=%u", channelCount, width);

			harness.requireEqual(plan.simdChannels + plan.pairedChannels + plan.singleChannels,
				channelCount, std::string(label) + ": plan covers every channel exactly once");
			harness.expectEqual(plan.simdChannels, channelCount / width * width,
				std::string(label) + ": SIMD group assignment must stay stable");
			harness.expectEqual(plan.pairedChannels % 2, 0u,
				std::string(label) + ": paired kernel takes whole pairs");
		}
	}
}

// The single-chain kernel runs one loop-carried dependency chain per channel,
// so it is latency-bound; any two channels it serves could instead overlap
// their chains. At most one leftover channel may land there. This is the
// stereo-on-wide-SIMD gap: floor dispatch left ch=2 width=4/8 fully single.
void testPlanDoesNotSerializePairableChannels()
{
	for (unsigned width : {2u, 4u, 8u})
	{
		for (unsigned channelCount = 1; channelCount <= 16; channelCount++)
		{
			const BiQuadKernelPlan plan = planBiQuadKernels(channelCount, width);
			char label[96];
			snprintf(label, sizeof(label),
				"ch=%u width=%u leaves %u channels to the single-chain kernel",
				channelCount, width, plan.singleChannels);
			harness.expect(plan.singleChannels <= 1, label);
		}
	}
}

// Renders `channelCount` channels of deterministic input through one
// multi-channel filter and through per-channel mono filters, and returns both
// outputs. Channel c gets the same input stream in both arrangements.
void renderMultiAndMono(unsigned channelCount, vector<double>& multiOut, vector<double>& monoOut)
{
	const wstring names[] = {L"C0", L"C1", L"C2", L"C3", L"C4", L"C5", L"C6", L"C7"};

	BiQuadFilter multi(BiQuad::PEAKING, 5.0, 997.0, 0.9, false, false);
	vector<wstring> multiNames(names, names + channelCount);
	multi.initialize(48000.0f, frameCount, multiNames);

	vector<BiQuadFilter> monos;
	monos.reserve(channelCount);
	for (unsigned c = 0; c < channelCount; c++)
	{
		monos.emplace_back(BiQuad::PEAKING, 5.0, 997.0, 0.9, false, false);
		vector<wstring> monoName(1, names[c]);
		monos.back().initialize(48000.0f, frameCount, monoName);
	}

	multiOut.assign((size_t)channelCount * blockCount * frameCount, 0.0);
	monoOut.assign((size_t)channelCount * blockCount * frameCount, 0.0);

	vector<double> inputData((size_t)channelCount * frameCount);
	vector<double> outputData((size_t)channelCount * frameCount);
	vector<double*> inputPtrs(channelCount);
	vector<double*> outputPtrs(channelCount);
	for (unsigned c = 0; c < channelCount; c++)
	{
		inputPtrs[c] = inputData.data() + (size_t)c * frameCount;
		outputPtrs[c] = outputData.data() + (size_t)c * frameCount;
	}

	unsigned long long state = 0x2545F4914F6CDD1DULL;
	for (unsigned block = 0; block < blockCount; block++)
	{
		for (unsigned c = 0; c < channelCount; c++)
			for (unsigned i = 0; i < frameCount; i++)
				inputPtrs[c][i] = nextSample(state);

		multi.process(outputPtrs.data(), inputPtrs.data(), frameCount);
		for (unsigned c = 0; c < channelCount; c++)
			memcpy(multiOut.data() + ((size_t)c * blockCount + block) * frameCount,
				outputPtrs[c], frameCount * sizeof(double));

		for (unsigned c = 0; c < channelCount; c++)
		{
			double* outPtr[1] = {outputPtrs[c]};
			double* inPtr[1] = {inputPtrs[c]};
			monos[c].process(outPtr, inPtr, frameCount);
			memcpy(monoOut.data() + ((size_t)c * blockCount + block) * frameCount,
				outputPtrs[c], frameCount * sizeof(double));
		}
	}
}

// A multi-channel filter must produce, bit for bit, what a mono filter
// produces per channel. 2 and 3 channels never reach the FMA SIMD group on
// targets wider than 2 lanes, and on 2-lane targets Highway MulAdd lowers to
// unfused mul+add matching the scalar expression, so exact equality holds on
// every CI-executed variant, before and after any remainder-kernel change.
void testMultiChannelMatchesMonoBitExactly()
{
	for (unsigned channelCount : {2u, 3u})
	{
		vector<double> multiOut, monoOut;
		renderMultiAndMono(channelCount, multiOut, monoOut);

		char label[96];
		snprintf(label, sizeof(label),
			"%u-channel output must match the mono filters bit for bit", channelCount);
		harness.expect(
			memcmp(multiOut.data(), monoOut.data(), multiOut.size() * sizeof(double)) == 0,
			label);
	}
}

// The engine processes every block under MxcsrFtzDazGuard, so a subnormal
// never reaches a filter's arithmetic: MXCSR FTZ|DAZ on x64, FPCR.FZ on ARM64
// (audit #348). Both flush a subnormal operand and a subnormal result, and
// the caller's mode comes back when the scope ends. The ARM64 leg of CI runs
// this on an ARM64 runner.
void testDenormalGuardFlushesAndRestores()
{
	volatile double subnormal = 4.9e-320;
	volatile double one = 1.0;
	{
		MxcsrFtzDazGuard guard;
		volatile double tiny = 1e-300;
		const double operand = subnormal * one;
		harness.expect(operand == 0.0, "a subnormal operand reads as zero inside the guard");
		const double result = tiny * 1e-10;
		harness.expect(result == 0.0, "a subnormal result is flushed to zero inside the guard");
	}
	const double kept = subnormal * one;
	harness.expect(kept != 0.0, "the caller's floating-point mode comes back when the guard ends");
}
}

void runBiQuadKernelTests()
{
	testBiQuadRejectsNonFiniteParameters();
	testBiQuadRejectsNonPositiveParameters();
	testBiQuadAtNyquistPassesThrough();
	testPlanCoversEveryChannelOnce();
	testPlanDoesNotSerializePairableChannels();
	testMultiChannelMatchesMonoBitExactly();
	testDenormalGuardFlushesAndRestores();

	harness.report();
}
