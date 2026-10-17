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
#include <windows.h>

#include "engine/FilterConfiguration.h"
#include "../TestHarness.h"

namespace
{

constexpr unsigned maxFrames = 480;

class GuardedInputPages
{
public:
	explicit GuardedInputPages(size_t pageSize)
		: allocation_(VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE, PAGE_NOACCESS))
	{
		if (allocation_ != nullptr &&
			VirtualAlloc(allocation_, pageSize, MEM_COMMIT, PAGE_READWRITE) == nullptr)
		{
			VirtualFree(allocation_, 0, MEM_RELEASE);
			allocation_ = nullptr;
		}
	}

	~GuardedInputPages()
	{
		if (allocation_ != nullptr)
			VirtualFree(allocation_, 0, MEM_RELEASE);
	}

	GuardedInputPages(const GuardedInputPages&) = delete;
	GuardedInputPages& operator=(const GuardedInputPages&) = delete;

	unsigned char* firstPage() const
	{
		return static_cast<unsigned char*>(allocation_);
	}

private:
	void* allocation_;
};

__declspec(noinline) double scalarPromoteOne(float value)
{
	return static_cast<double>(value);
}

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
				{
					const double expected = static_cast<double>(floatIn[(size_t)i * channels + c]);
					readFloatOk = readFloatOk && memcmp(&planar[c][i], &expected, sizeof(double)) == 0;
				}
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

// Pin the stereo kernel's boundary behavior separately from the broad format
// matrix above. The exact-end placement makes any input read past the requested
// frames touch an uncommitted page. Moving the input four bytes earlier leaves
// one trailing float and exercises the other common SIMD alignment. For zero
// frames, the exact-end input points at the protected page itself.
TEST_CASE(EngineOrchestrationTests, testStereoFloatReadIsBitExactAtTailsAndGuardPage)
{
	constexpr unsigned frameCounts[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
		31, 32, 33, 63, 64, 65, 127, 128, 129, 479, 480, 481
	};
	constexpr uint32_t floatPatterns[] = {
		0x00000000u, 0x80000000u, // Positive and negative zero.
		0x00000001u, 0x007FFFFFu, 0x80000001u, 0x807FFFFFu, // Subnormal extrema, both signs.
		0x00800000u, 0x80800000u, // Minimum normal, both signs.
		0x7F7FFFFFu, 0xFF7FFFFFu, // Maximum finite, both signs.
		0x7F800000u, 0xFF800000u, // Infinities.
		0x7FC00001u, 0x7FC12345u, 0xFFC00001u, 0xFFD54321u, // Quiet NaNs, varied payloads/signs.
		0x3F800000u, 0xBF000001u, 0x3EAAAAABu, 0xC0490FDBu, 0x41234567u, 0xB7654321u
	};
	constexpr size_t patternCount = sizeof(floatPatterns) / sizeof(floatPatterns[0]);
	constexpr uint32_t trailingBits = 0x3F4CCCCDu;
	constexpr uint64_t sentinelBits = 0xC0DDBAADF00D1234ULL;
	constexpr size_t largestInputBytes = (481u * 2u + 1u) * sizeof(float);

	// Signaling NaNs are deliberately omitted. A conversion may quiet them, and
	// which payload bits survive that operation is platform-specific. Quiet NaNs
	// still compare the kernel's sign and payload result with a scalar cast on
	// the same target.
	SYSTEM_INFO systemInfo = {};
	GetSystemInfo(&systemInfo);
	const size_t pageSize = systemInfo.dwPageSize;
	GuardedInputPages pages(pageSize);
	harness.expect(pages.firstPage() != nullptr,
		"stereo guarded input: VirtualAlloc failed to reserve two pages and commit the first");
	if (pages.firstPage() == nullptr)
		return;

	char label[320];
	snprintf(label, sizeof(label),
		"stereo guarded input: page size %zu holds the largest %zu-byte case",
		pageSize, largestInputBytes);
	harness.expect(pageSize >= largestInputBytes, label);
	if (pageSize < largestInputBytes)
		return;

	double sentinel;
	memcpy(&sentinel, &sentinelBits, sizeof(sentinel));
	size_t caseIndex = 0;
	for (unsigned frames : frameCounts)
	{
		for (unsigned trailingFloats : {0u, 1u})
		{
			const size_t inputSampleCount = static_cast<size_t>(frames) * 2;
			const size_t inputByteCount = (inputSampleCount + trailingFloats) * sizeof(float);
			const size_t inputByteOffset = pageSize - inputByteCount;
			float* input = reinterpret_cast<float*>(pages.firstPage() + inputByteOffset);
			const size_t inputMod16 = reinterpret_cast<uintptr_t>(input) & 15u;

			// Make a different pattern finish each case. Across the length/alignment
			// matrix every special value therefore appears in scalar tails as well
			// as in the main body of longer reads.
			const size_t tailPatternIndex = caseIndex % patternCount;
			const size_t patternOffset = inputSampleCount == 0 ? tailPatternIndex :
				(tailPatternIndex + patternCount - (inputSampleCount - 1) % patternCount) % patternCount;
			for (size_t i = 0; i < inputSampleCount; i++)
			{
				const uint32_t bits = floatPatterns[(patternOffset + i) % patternCount];
				memcpy(input + i, &bits, sizeof(bits));
			}
			if (trailingFloats != 0)
				memcpy(input + inputSampleCount, &trailingBits, sizeof(trailingBits));

			// This is always odd and greater than frames. The second planar channel
			// consequently starts eight bytes past a 16-byte boundary when the
			// vector allocation has the platform's normal 16-byte alignment.
			const unsigned capacity = frames + (frames % 2 != 0 ? 2u : 1u);
			FilterConfiguration config(EngineStreamFormat{2, 2, capacity}, {}, 2);
			double** planar = config.getOutputSamples();
			for (unsigned channel = 0; channel < 2; channel++)
				std::fill(planar[channel], planar[channel] + capacity, sentinel);

			config.readFloatInterleaved(input, frames);

			for (unsigned frame = 0; frame < frames; frame++)
			{
				for (unsigned channel = 0; channel < 2; channel++)
				{
					const size_t inputIndex = static_cast<size_t>(frame) * 2 + channel;
					const size_t patternIndex = (patternOffset + inputIndex) % patternCount;
					float source;
					memcpy(&source, input + inputIndex, sizeof(source));
					const double expected = scalarPromoteOne(source);
					snprintf(label, sizeof(label),
						"stereo promote frames=%u inputOffset=%zu mod16=%zu trailing=%u "
						"patternOffset=%zu frame=%u channel=%u pattern[%zu]=0x%08X differs from scalar",
						frames, inputByteOffset, inputMod16, trailingFloats, patternOffset,
						frame, channel, patternIndex, static_cast<unsigned>(floatPatterns[patternIndex]));
					harness.expect(memcmp(&planar[channel][frame], &expected, sizeof(expected)) == 0, label);
				}
			}

			for (unsigned channel = 0; channel < 2; channel++)
			{
				for (unsigned frame = frames; frame < capacity; frame++)
				{
					snprintf(label, sizeof(label),
						"stereo promote frames=%u inputOffset=%zu mod16=%zu trailing=%u "
						"patternOffset=%zu wrote suffix channel=%u frame=%u",
						frames, inputByteOffset, inputMod16, trailingFloats, patternOffset, channel, frame);
					harness.expect(memcmp(&planar[channel][frame], &sentinel, sizeof(sentinel)) == 0, label);
				}
			}
			caseIndex++;
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
// through volatiles, and noinline prevents specialization into the caller.
// The compiler may still optimize or auto-vectorize these functions; this is
// a source-shape reference, not a guarantee of scalar machine instructions.
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

// The stereo float conversions against the runtime-shape strided reference.
//
// The candidate is the product code: writeFloatInterleaved and
// readFloatInterleaved come from Common.lib, compiled as C++20 with the build
// variant's instruction set, exactly as the APO ships them. The reference is
// the two functions above. The write gate detects a throughput regression
// relative to that reference; timings do not identify which instructions ran.
//
// x86 gates the WRITE at 1.3x. Measured on the CI runners (audit #422): sse2
// 1.62x, avx 2.56x, avx2 2.07x. Reads remain measurement-only. Before the
// stereo-read rework, SSE2 read measured 0.97x on CI and ARM64 read 1.03x.
// The old ARM64 product used Highway EMU128, with stack-based lane assembly,
// not native NEON deinterleaving (verified against the matching DLL/PDB).
// ARM64 has no speed gate; its write still uses the strided loop. Keep the
// same reference and thresholds while measuring the new read kernels.
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
		std::printf("  [SampleIo] stereo read: reference %.0f ns vs %.0f ns (%.2fx); "
			"write: reference %.0f ns vs %.0f ns (%.2fx)\n",
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
		"stereo float write is only %.2fx the runtime-shape reference (needs >= %.1fx)",
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
