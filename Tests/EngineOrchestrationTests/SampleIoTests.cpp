/*
	This file is part of EqualizerAPO-XT.

	FilterConfiguration sample-I/O tests: the fused format conversions
	(float/double x interleaved/planar) between the APO-facing buffers and the
	engine's internal planar double storage.

	Two contracts are pinned here. Equivalence: every conversion path must
	produce, bit for bit, what the naive per-channel scalar loop produces —
	promote is exact and demote rounds to nearest-even exactly like
	static_cast<float>, so vectorizing a conversion must never change output
	bits. Throughput: the stereo float round trip (the common APO
	configuration) must beat the naive scalar loop shape by a clear margin;
	this is the regression test for the 2026-07 finding that the interleaved
	conversions ran one strided scalar element at a time.
*/

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "engine/FilterConfiguration.h"
#include "engine/FilterEngine.h"
#include "Tests/TestHarness.h"

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

// An engine initialized from an empty temp config; FilterConfiguration only
// needs its channel counts and max frame count.
struct IoFixture
{
	FilterEngine engine;
	std::wstring configPath;

	IoFixture(test::Harness& harness, unsigned channels)
	{
		wchar_t tempPath[MAX_PATH] = {};
		DWORD len = GetTempPathW(MAX_PATH, tempPath);
		std::wstring dir = (len > 0 && len < MAX_PATH) ? tempPath : L".\\";
		configPath = dir + L"SampleIoTests-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
		std::ofstream stream(configPath, std::ios::binary | std::ios::trunc);
		stream << "# empty\n";
		stream.close();
		if (!stream)
			harness.fail("could not write SampleIoTests temp config");

		const std::wstring deviceName = L"SampleIoTests";
		engine.setDeviceInfo(false, true, deviceName, L"File", L"", deviceName + L" File");
		engine.initialize(48000.0f, channels, channels, channels, 0, maxFrames, configPath);
	}

	~IoFixture()
	{
		DeleteFileW(configPath.c_str());
	}
};

// Every conversion path must match the naive scalar loop bit for bit.
void testConversionsMatchScalarReferenceBitExactly(test::Harness& harness)
{
	for (unsigned channels : {1u, 2u, 3u, 4u, 6u, 8u})
	{
		IoFixture fixture(harness, channels);

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
			FilterConfiguration config(&fixture.engine, {}, channels);
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

			// Planar float pair: promote in, demote out.
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

// The stereo float conversions must clearly beat the naive strided scalar
// loop — in the direction each platform actually vectorizes.
//
// x86 gates the WRITE: the MSVC auto-vectorizer already lifts the
// constant-stride loops to ~2.1x the naive reference (both directions),
// explicit StoreInterleaved2 reaches 2.8x (SSE2) to 5.1x (AVX2), so the
// 2.4x bar separates the explicit-SIMD write from the merely
// auto-vectorized one. The read direction cannot serve as the contract
// there: auto-vectorized strided loads already sit at 2.1x and the Highway
// read is not uniformly ahead of that on 2-lane targets.
//
// ARM64 gates the READ (LoadInterleaved2, measured 2.7x): MSVC ARM64
// lowers Highway's interleaved STORES below scalar speed in every shape
// measured on the CI runner (half-width 0.99x, full-width Combine 0.47x),
// so the write kernels keep the scalar loop there and a write bar would be
// meaningless. Both directions are always measured and printed for the
// benchmark record; three attempts absorb CI scheduling outliers.
void testStereoFloatConversionBeatsScalarReference(test::Harness& harness)
{
	constexpr unsigned channels = 2;
	constexpr unsigned frames = maxFrames;
#ifdef _M_ARM64
	constexpr bool gateWrite = false;
	constexpr double requiredRatio = 1.5;
#else
	constexpr bool gateWrite = true;
	constexpr double requiredRatio = 2.4;
#endif
	IoFixture fixture(harness, channels);
	FilterConfiguration config(&fixture.engine, {}, channels);

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

	auto referenceRead = [&]() {
		for (size_t c = 0; c < channels; c++)
		{
			double* sampleChannel = refPlanar[c];
			const float* src = in + c;
			for (size_t i = 0; i < frames; i++)
				sampleChannel[i] = static_cast<double>(src[i * channels]);
		}
	};
	auto referenceWrite = [&]() {
		for (size_t c = 0; c < channels; c++)
		{
			const double* sampleChannel = refPlanar[c];
			float* dst = out + c;
			for (size_t i = 0; i < frames; i++)
				dst[i * channels] = static_cast<float>(sampleChannel[i]);
		}
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

	double bestRatio = 0.0;
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
		const double gatedRatio = gateWrite ? refWriteSeconds / candWriteSeconds
			: refReadSeconds / candReadSeconds;
		bestRatio = std::max(bestRatio, gatedRatio);
		if (bestRatio >= requiredRatio)
			break;
	}

	char label[160];
	snprintf(label, sizeof(label),
		"stereo float %s is only %.2fx the naive scalar loop (needs >= %.1fx); "
		"the interleaved conversion still runs one strided element at a time",
		gateWrite ? "write" : "read", bestRatio, requiredRatio);
	harness.expect(bestRatio >= requiredRatio, label);

	// The measured loops must have produced real output (keeps the work live).
	harness.expect(output[1] == output[1] && refPlanarData[0] == refPlanarData[0],
		"timing loops produced NaN");
}

} // namespace

void runSampleIoTests(test::Harness& harness)
{
	testConversionsMatchScalarReferenceBitExactly(harness);
	testStereoFloatConversionBeatsScalarReference(harness);
}
