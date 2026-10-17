/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	FilterConfiguration sample-I/O tests: the fused format conversions
	(float/double x interleaved/planar) between the APO-facing buffers and the
	engine's internal planar double storage.

	Two contracts are pinned here. Equivalence: every conversion path must
	produce, bit for bit, what the naive per-channel scalar loop produces —
	promote is exact and demote rounds to nearest-even exactly like
	static_cast<float>, so vectorizing a conversion must never change output
	bits. Throughput: on x86 the stereo float write (the common APO
	configuration) must beat the naive scalar loop shape by a clear margin;
	this is the regression test for the 2026-07 finding that the interleaved
	conversions ran one strided scalar element at a time. ARM64 measures the
	same loops for the record and has no bar.
*/

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include "engine/FilterConfiguration.h"
#include "../TestHarness.h"

namespace
{

constexpr unsigned maxFrames = 480;

// Deterministic sample stream; xorshift64* mapped into [-1, 1].
double nextSample(unsigned long long& state)
{
	state ^= state >> 12;
	state ^= state << 25;
	state ^= state >> 27;
	unsigned long long scrambled = state * 2685821657736338717ULL;
	return static_cast<double>(scrambled >> 11) / static_cast<double>(1ULL << 53) * 2.0 - 1.0;
}

// Audit #250 A2: FilterConfiguration takes the three stream facts directly,
// so these tests no longer boot a FilterEngine (with its temp config file,
// registry read and factory registry) per channel count.
EngineStreamFormat formatFor(unsigned channels)
{
	return EngineStreamFormat{ channels, channels, maxFrames };
}

// Every conversion path must match the naive scalar loop bit for bit.
TEST_CASE(EngineOrchestrationTests, testConversionsMatchScalarReferenceBitExactly)
{
	for (unsigned channels : {1u, 2u, 3u, 4u, 6u, 8u})
	{
		for (unsigned frames : {480u, 61u})
		{
			char label[96];
			unsigned long long state = 0x9E3779B97F4A7C15ULL + channels * 131 + frames;

			std::vector<float> floatIn((size_t)frames * channels);
			std::vector<double> doubleIn((size_t)frames * channels);
			for (size_t i = 0; i < floatIn.size(); i++)
			{
				doubleIn[i] = nextSample(state);
				floatIn[i] = static_cast<float>(doubleIn[i]);
			}

			// readFloatInterleaved: planar double result vs scalar promote.
			FilterConfiguration config(formatFor(channels), {}, channels);
			config.readFloatInterleaved(floatIn.data(), frames);
			double** planar = config.getOutputSamples();
			bool readFloatOk = true;
			for (unsigned c = 0; c < channels; c++)
				for (unsigned i = 0; i < frames; i++)
					readFloatOk = readFloatOk &&
						planar[c][i] == static_cast<double>(floatIn[(size_t)i * channels + c]);
			snprintf(label, sizeof(label), "readFloatInterleaved ch=%u frames=%u differs from scalar", channels, frames);
			harness.expect(readFloatOk, label);

			// read(double*): planar double result vs plain deinterleave.
			config.read(doubleIn.data(), frames);
			bool readDoubleOk = true;
			for (unsigned c = 0; c < channels; c++)
				for (unsigned i = 0; i < frames; i++)
					readDoubleOk = readDoubleOk &&
						planar[c][i] == doubleIn[(size_t)i * channels + c];
			snprintf(label, sizeof(label), "read(double*) ch=%u frames=%u differs from scalar", channels, frames);
			harness.expect(readDoubleOk, label);

			// writeFloatInterleaved from the planar state read above.
			std::vector<float> floatOut((size_t)frames * channels, -2.0f);
			config.writeFloatInterleaved(floatOut.data(), frames);
			bool writeFloatOk = true;
			for (unsigned c = 0; c < channels; c++)
				for (unsigned i = 0; i < frames; i++)
				{
					const float expected = static_cast<float>(planar[c][i]);
					const float actual = floatOut[(size_t)i * channels + c];
					writeFloatOk = writeFloatOk && memcmp(&actual, &expected, sizeof(float)) == 0;
				}
			snprintf(label, sizeof(label), "writeFloatInterleaved ch=%u frames=%u differs from scalar", channels, frames);
			harness.expect(writeFloatOk, label);

			// write(double*): interleave back and compare to the source.
			std::vector<double> doubleOut((size_t)frames * channels, -2.0);
			config.write(doubleOut.data(), frames);
			bool writeDoubleOk = true;
			for (size_t i = 0; i < (size_t)frames * channels; i++)
				writeDoubleOk = writeDoubleOk && doubleOut[i] == doubleIn[i];
			snprintf(label, sizeof(label), "write(double*) ch=%u frames=%u differs from scalar", channels, frames);
			harness.expect(writeDoubleOk, label);

			// Planar float pair: promote in, demote out (the VoicemeeterClient
			// surface).
			std::vector<float> planarIn((size_t)frames * channels);
			std::vector<const float*> planarInPtrs(channels);
			std::vector<float> planarOut((size_t)frames * channels, -2.0f);
			std::vector<float*> planarOutPtrs(channels);
			for (unsigned c = 0; c < channels; c++)
			{
				planarInPtrs[c] = planarIn.data() + (size_t)c * frames;
				planarOutPtrs[c] = planarOut.data() + (size_t)c * frames;
			}
			for (size_t i = 0; i < planarIn.size(); i++)
				planarIn[i] = static_cast<float>(nextSample(state));

			config.readFloatPlanar(planarInPtrs.data(), frames);
			bool planarOk = true;
			for (unsigned c = 0; c < channels; c++)
				for (unsigned i = 0; i < frames; i++)
					planarOk = planarOk && planar[c][i] == static_cast<double>(planarInPtrs[c][i]);
			snprintf(label, sizeof(label), "readFloatPlanar ch=%u frames=%u differs from scalar", channels, frames);
			harness.expect(planarOk, label);

			config.writeFloatPlanar(planarOutPtrs.data(), frames);
			bool planarOutOk = true;
			for (unsigned c = 0; c < channels; c++)
				for (unsigned i = 0; i < frames; i++)
				{
					const float expected = static_cast<float>(planar[c][i]);
					planarOutOk = planarOutOk && memcmp(&planarOutPtrs[c][i], &expected, sizeof(float)) == 0;
				}
			snprintf(label, sizeof(label), "writeFloatPlanar ch=%u frames=%u differs from scalar", channels, frames);
			harness.expect(planarOutOk, label);

		}
	}
}

// One timing sample: the better (smaller) of `samples` measurements of
// `batch` calls of fn. min-of-N discards scheduler noise; what remains is the
// code's floor.
template<typename Fn>
double minBatchSeconds(Fn&& fn, int samples, int batch)
{
	double best = 1e100;
	for (int s = 0; s < samples; s++)
	{
		const auto start = std::chrono::steady_clock::now();
		for (int b = 0; b < batch; b++)
			fn();
		const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
		best = std::min(best, elapsed.count());
	}
	return best;
}

// The yardstick of the throughput contract below: the one strided element at
// a time loop the conversions used to be (the 2026-07 finding), with its
// shape taken at run time as theirs was. The callers pass channels and frames
// through volatiles, and noinline keeps this code the same however the test
// around it is compiled, inlined or registered.
__declspec(noinline) void naiveReadInterleaved(const float* in, double* const* planar,
	size_t channels, size_t frames)
{
	for (size_t c = 0; c < channels; c++)
	{
		double* sampleChannel = planar[c];
		const float* src = in + c;
		for (size_t i = 0; i < frames; i++)
			sampleChannel[i] = static_cast<double>(src[i * channels]);
	}
}

__declspec(noinline) void naiveWriteInterleaved(const double* const* planar, float* out,
	size_t channels, size_t frames)
{
	for (size_t c = 0; c < channels; c++)
	{
		const double* sampleChannel = planar[c];
		float* dst = out + c;
		for (size_t i = 0; i < frames; i++)
			dst[i * channels] = static_cast<float>(sampleChannel[i]);
	}
}

// The stereo float conversions against the naive strided scalar loop.
//
// The candidate is the product code: writeFloatInterleaved and
// readFloatInterleaved come from Common.lib, compiled as C++20 with the build
// variant's instruction set, exactly as the APO ships them. The reference is
// the two functions above. So the bar asks "is the shipped conversion still
// clearly faster than the naive loop it replaced", and it fails if the
// product code ever falls back to that shape. It does not ask how much the
// hand-written SIMD gains in every direction; the printed timings are the
// record for that, not a gate.
//
// x86 gates the WRITE at 1.3x. Measured on the CI runners (audit #422): sse2
// 1.62x, avx 2.56x, avx2 2.07x. A scalar loop measures about 1.0x (the ARM64
// write, which keeps one, measured 1.14x). The read cannot serve as the
// contract: sse2 reads at 0.97x.
//
// ARM64 has no bar. The read (LoadInterleaved2) measured 1.03x on the CI
// runner, and the write keeps the scalar loop because MSVC ARM64 lowers
// Highway's interleaved stores below scalar speed, so no ratio there tells
// the SIMD kernel from a fallback. The loops still run and print.
//
// Why the reference is not a lambda in this test (audit #422, replacing the
// audit #348 TD-74 calibration). The lambdas it used to be ran at whatever
// speed the test's own compile gave them, with the candidate unchanged each
// time: on the avx2 runner the write reference took 1650 ns pinned to C++17
// and 295 ns under C++20; on the ARM64 runner 775 ns while the test was
// inlined into one runner function and 350 ns once it was registered by
// pointer (T2). The old bars (2.4x write on x86, 1.5x read on ARM64) were set
// against the slow builds. The functions above measured 311 ns (avx2 runner)
// and 331 ns (ARM64 runner). Three attempts absorb CI scheduling outliers.
//
// This TU is still pinned to /std:c++17 in the project file. The pin was for
// the lambdas; the contract no longer depends on it.
TEST_CASE(EngineOrchestrationTests, testStereoFloatConversionBeatsScalarReference)
{
	// Timing contracts are meaningless under AddressSanitizer: the
	// instrumentation cost lands on the vectorized candidate, not on the
	// reference loop shape, so the ratio inverts regardless of the code under
	// test. The memcheck CI leg builds with /fsanitize=address and still runs
	// the bit-exactness test above.
#ifdef __SANITIZE_ADDRESS__
	return;
#endif
	constexpr unsigned channels = 2;
	constexpr unsigned frames = maxFrames;
	constexpr double requiredWriteRatio = 1.3;
	FilterConfiguration config(formatFor(channels), {}, channels);

	std::vector<float> input((size_t)frames * channels);
	std::vector<float> output((size_t)frames * channels);
	unsigned long long state = 0xC0FFEE123456789ULL;
	for (size_t i = 0; i < input.size(); i++)
		input[i] = static_cast<float>(nextSample(state));

	std::vector<double> refPlanarData((size_t)frames * channels);
	double* refPlanar[channels] = {refPlanarData.data(), refPlanarData.data() + frames};

	const float* in = input.data();
	float* out = output.data();

	// Fill both planar states once so the write loops convert real data.
	config.readFloatInterleaved(in, frames);
	for (size_t c = 0; c < channels; c++)
		for (size_t i = 0; i < frames; i++)
			refPlanar[c][i] = static_cast<double>(in[i * channels + c]);

	volatile size_t shapeChannels = channels;
	volatile size_t shapeFrames = frames;
	auto referenceRead = [&]() {
		naiveReadInterleaved(in, refPlanar, shapeChannels, shapeFrames);
	};
	auto referenceWrite = [&]() {
		naiveWriteInterleaved(refPlanar, out, shapeChannels, shapeFrames);
	};
	auto candidateRead = [&]() {
		config.readFloatInterleaved(in, frames);
	};
	auto candidateWrite = [&]() {
		config.writeFloatInterleaved(out, frames);
	};

	// Warm up code and data once so neither side pays first-touch costs.
	referenceRead();
	referenceWrite();
	candidateRead();
	candidateWrite();

	double bestWriteRatio = 0.0;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		const double refReadSeconds = minBatchSeconds(referenceRead, 80, 64);
		const double candReadSeconds = minBatchSeconds(candidateRead, 80, 64);
		const double refWriteSeconds = minBatchSeconds(referenceWrite, 80, 64);
		const double candWriteSeconds = minBatchSeconds(candidateWrite, 80, 64);
		std::printf("  [SampleIo] stereo read: scalar %.0f ns vs %.0f ns (%.2fx); "
			"write: scalar %.0f ns vs %.0f ns (%.2fx)\n",
			refReadSeconds / 64 * 1e9, candReadSeconds / 64 * 1e9, refReadSeconds / candReadSeconds,
			refWriteSeconds / 64 * 1e9, candWriteSeconds / 64 * 1e9, refWriteSeconds / candWriteSeconds);
		bestWriteRatio = std::max(bestWriteRatio, refWriteSeconds / candWriteSeconds);
		if (bestWriteRatio >= requiredWriteRatio)
			break;
	}

	// ARM64 has measured and printed the same loops; it has no bar (see above).
#ifndef _M_ARM64
	char label[160];
	snprintf(label, sizeof(label),
		"stereo float write is only %.2fx the naive scalar loop (needs >= %.1fx); "
		"the interleaved conversion still runs one strided element at a time",
		bestWriteRatio, requiredWriteRatio);
	harness.expect(bestWriteRatio >= requiredWriteRatio, label);
#endif

	// The measured loops must have produced real output (keeps the work live).
	harness.expect(output[1] == output[1] && refPlanarData[0] == refPlanarData[0],
		"timing loops produced NaN");
}

} // namespace

// Audit #348 TD-41/A3: the planar write covers every output channel, like the
// interleaved writes. With one input and two outputs (a mono source feeding a
// stereo device) it used to stop after channel 0 and leave channel 1 holding
// whatever the caller's buffer held; with more inputs than outputs it wrote
// past the caller's pointer array.
TEST_CASE(EngineOrchestrationTests, testFloatPlanarWriteCoversEveryOutputChannel)
{
	constexpr unsigned frames = 64;
	const EngineStreamFormat monoToStereo{ 1, 2, maxFrames };
	FilterConfiguration config(monoToStereo, {}, 2);

	std::vector<float> input(frames);
	for (unsigned i = 0; i < frames; i++)
		input[i] = static_cast<float>(i) / frames;
	const float* inputs[1] = { input.data() };
	config.readFloatPlanar(inputs, frames);

	std::vector<float> left(frames, -2.0f);
	std::vector<float> right(frames, -2.0f);
	float* outputs[2] = { left.data(), right.data() };
	config.writeFloatPlanar(outputs, frames);

	bool leftCopied = true;
	bool rightWritten = true;
	for (unsigned i = 0; i < frames; i++)
	{
		leftCopied = leftCopied && left[i] == input[i];
		rightWritten = rightWritten && right[i] != -2.0f;
	}
	harness.expect(leftCopied, "writeFloatPlanar mono->stereo: channel 0 carries the input");
	harness.expect(rightWritten, "writeFloatPlanar mono->stereo: channel 1 is written, not left untouched");
}
