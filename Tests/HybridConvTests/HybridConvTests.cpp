/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Simple regression tests for the libHybridConv bridge. These tests are
	kept framework-free so they can run wherever the Visual Studio build runs.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "filters/ConvolutionFilter.h"
#include "filters/ConfigFileReference.h"
#include "filters/IrCache.h"
#include "services/logging/Logging.h"
#include "services/registry/RegistryPaths.h"
#include "services/registry/RegistrySettings.h"
#include "runtime/memory/AlignedMemory.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"
#include "Tests/TestDirectory.h"
#include "Tests/FakeRegistry.h"
#include "../TestHarness.h"
#include "Tests/WavFixtures.h"
#include "Tests/AlignedMemoryGate.h"

// Additional suites in this binary register their TEST_CASE definitions from
// the correspondingly named XxxTests.cpp files.
using std::string;
using std::vector;
using std::wstring;

namespace
{
constexpr int frameLength = 480;
constexpr int sampleRate = 48000;
constexpr double tolerance = 1.0e-8;

test::HarnessAccess harness;

test::TestDirectory& scratchDirectory()
{
	static test::TestDirectory directory(L"HybridConvTests");
	return directory;
}

wstring wisdomTestDirectory;
wstring previousLocalAppData;

TEST_CASE(HybridConvTests, assertFftwWisdomIsExported)
{
	// Audit #467 TD-61: the path must survive outside the active ANSI code page.
	// The universal escape keeps this C++ source ASCII-only.
	wisdomTestDirectory = scratchDirectory().subdirectory(L"wisdom-☃");
	const DWORD directoryAttributes = GetFileAttributesW(wisdomTestDirectory.c_str());
	harness.expectTrue(directoryAttributes != INVALID_FILE_ATTRIBUTES
			&& (directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
		"FFTW wisdom test creates its stand-in LOCALAPPDATA directory");

	wchar_t oldLocalAppData[MAX_PATH] = {};
	const DWORD oldLength = GetEnvironmentVariableW(
		L"LOCALAPPDATA", oldLocalAppData, MAX_PATH);
	if (oldLength > 0 && oldLength < MAX_PATH)
		previousLocalAppData.assign(oldLocalAppData, oldLength);
	_wputenv_s(L"LOCALAPPDATA", wisdomTestDirectory.c_str());

	double impulse[] = {1.0};
	HConvSingle filter = {};
	hcInitSingle(&filter, impulse, 1, 64, 1);
	hcCloseSingle(&filter);

	const wstring wisdomPath =
		wisdomTestDirectory + L"\\EqualizerAPO\\fftw_wisdom.dat";
	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	harness.expectTrue(GetFileAttributesExW(
		wisdomPath.c_str(), GetFileExInfoStandard, &attributes) != 0,
		"first convolution plan exports FFTW wisdom");
	harness.expectTrue(attributes.nFileSizeHigh != 0 || attributes.nFileSizeLow != 0,
		"exported FFTW wisdom is not empty");
}

void cleanupFftwWisdomTest()
{
	const wstring wisdomDir = wisdomTestDirectory + L"\\EqualizerAPO";
	DeleteFileW((wisdomDir + L"\\fftw_wisdom.dat").c_str());
	RemoveDirectoryW(wisdomDir.c_str());
	RemoveDirectoryW(wisdomTestDirectory.c_str());
	_wputenv_s(L"LOCALAPPDATA", previousLocalAppData.c_str());
}

struct Tap
{
	int sample;
	double value;
};

void fail(const string& message)
{
	harness.fail(message);
}

void expectClose(double actual, double expected, int sample)
{
	harness.expectNear(actual, expected, tolerance, "sample " + std::to_string(sample));
}

vector<double> renderImpulseResponse(const vector<double>& impulseResponse, int leadingSilentFrames)
{
	HConvSingle filter = {};
	hcInitSingle(&filter, const_cast<double*>(impulseResponse.data()), static_cast<int>(impulseResponse.size()), frameLength, 1);

	const int framesToRender = leadingSilentFrames + static_cast<int>((impulseResponse.size() + frameLength - 1) / frameLength) + 3;
	vector<double> input(frameLength, 0.0);
	vector<double> output(frameLength, 0.0);
	vector<double> rendered((size_t)framesToRender * frameLength, 0.0);

	for (int frame = 0; frame < framesToRender; ++frame)
	{
		fill(input.begin(), input.end(), 0.0);
		fill(output.begin(), output.end(), 0.0);

		if (frame == leadingSilentFrames)
			input[0] = 1.0;

		hcPutSingle(&filter, input.data());
		hcProcessSingle(&filter);
		hcGetSingle(&filter, output.data());

		copy(output.begin(), output.end(), rendered.begin() + (size_t)frame * frameLength);
	}

	hcCloseSingle(&filter);
	return rendered;
}

wstring createImpulseResponseFile(const vector<double>& impulseResponse)
{
	static unsigned fileCount = 0;
	const wstring filename = scratchDirectory().trackFile(L"ir-" + std::to_wstring(fileCount++) + L".wav");
	harness.require(test::writeWavFile(filename, sampleRate, impulseResponse),
		"the temporary impulse response file is written completely");
	return filename;
}

TEST_CASE(HybridConvTests, assertTrailingZerosAreTrimmed)
{
	const vector<vector<double>> buffers = {{0.5, -0.25, 0.0, 0.0}};
	harness.expectTrue(trailingSilenceTrimmedLength(buffers, 4) == 2,
		"trailing zero frames should be removed");
}

TEST_CASE(HybridConvTests, assertRelativeThresholdKeepsAllChannelsAligned)
{
	const vector<vector<double>> buffers = {
		{0.0, 1.0, 0.0, 0.0, 0.0, 0.0},
		{0.0, 0.0, 0.0, 1.000001e-7, 0.0, 0.999999e-7},
	};
	harness.expectTrue(trailingSilenceTrimmedLength(buffers, 6) == 4,
		"a sample above -140 dB in one channel should keep its frame for every channel");
}

TEST_CASE(HybridConvTests, assertLeadingSilenceIsRetained)
{
	const vector<vector<double>> buffers = {{0.0, 0.0, 1.0, 0.0}};
	harness.expectTrue(trailingSilenceTrimmedLength(buffers, 4) == 3,
		"leading silence should remain before the last significant frame");
}

TEST_CASE(HybridConvTests, assertAllZeroIrRetainsOneFrame)
{
	const vector<vector<double>> buffers = {{0.0, 0.0}, {0.0, 0.0}};
	harness.expectTrue(trailingSilenceTrimmedLength(buffers, 2) == 1,
		"an all-zero impulse response should retain one frame");
}

TEST_CASE(HybridConvTests, assertNonSilentTailKeepsOriginalLength)
{
	const vector<vector<double>> buffers = {{0.25, 0.0, -0.5}};
	harness.expectTrue(trailingSilenceTrimmedLength(buffers, 3) == 3,
		"a significant final frame should keep the original length");
}

TEST_CASE(HybridConvTests, assertIgnoreConvolutionSilenceRegistryPolicy)
{
	test::FakeRegistry registry;
	harness.expectTrue(readIgnoreConvolutionSilence(registry),
		"a missing application key should enable silence removal");
	registry.seedKey(APP_REGPATH);
	harness.expectTrue(readIgnoreConvolutionSilence(registry),
		"a missing value should enable silence removal");
	registry.seedString(APP_REGPATH, ignoreConvolutionSilenceValueName, L"false");
	harness.expectTrue(!readIgnoreConvolutionSilence(registry),
		"the exact string false should disable silence removal");
	registry.seedString(APP_REGPATH, ignoreConvolutionSilenceValueName, L"False");
	harness.expectTrue(readIgnoreConvolutionSilence(registry),
		"a differently cased value should enable silence removal");
	registry.seedDword(APP_REGPATH, ignoreConvolutionSilenceValueName, 0);
	harness.expectTrue(readIgnoreConvolutionSilence(registry),
		"an unreadable value type should enable silence removal");
	registry.denyRead(APP_REGPATH);
	harness.expectTrue(readIgnoreConvolutionSilence(registry),
		"a denied registry read should enable silence removal");
}

TEST_CASE(HybridConvTests, assertIrCacheSeparatesSilenceRemovalSetting)
{
	const wstring filename = scratchDirectory().trackFile(L"cache-silence-setting.wav");
	const vector<vector<double>> channels = {
		{0.0, 1.0, 0.0, 0.0, 0.0, 0.0},
		{0.0, 0.0, 0.0, 2.0e-7, 0.0, 0.0},
	};
	harness.require(test::writeWavFile(filename, sampleRate, channels),
		"the cache-setting impulse response is written completely");
	{
		const auto target = ConfigFileReference::target(L"", filename);
		harness.require(static_cast<bool>(target), "the cache-setting impulse response is judged");
		auto full = loadIrCached(target.path, sampleRate, false);
		auto trimmed = loadIrCached(target.path, sampleRate, true);
		harness.require(full != nullptr && trimmed != nullptr,
			"both silence-removal settings load the same impulse response");
		harness.expectTrue(full != trimmed,
			"silence-removal settings should use different cache entries");
		harness.expectTrue(full->frames == 6 && trimmed->frames == 4,
			"only the enabled cache entry should remove trailing silence");
		harness.expectTrue(trimmed->buffers[0].size() == 4 && trimmed->buffers[1].size() == 4,
			"silence removal should resize every channel to the same frame count");
	}
	DeleteFileW(filename.c_str());
}

TEST_CASE(HybridConvTests, assertJudgedIrCacheReuse)
{
	const wstring filename = createImpulseResponseFile({1.0, 0.5, 0.25});
	_wputenv_s(L"EAPO_XT_JUDGED_IR", filename.c_str());
	{
		const auto first = ConfigFileReference::target(L"", L"\"%EAPO_XT_JUDGED_IR%\"");
		const auto second = ConfigFileReference::target(L"", L"\"%EAPO_XT_JUDGED_IR%\"");
		auto left = loadIrCached(first.path, sampleRate, true);
		auto right = loadIrCached(second.path, sampleRate, true);
		harness.require(left != nullptr, "quoted environment IR loads through its held handle");
		harness.expectTrue(left == right, "second handle-backed IR load reuses the decoded cache entry");
		harness.expectTrue(left->frames == 3 && left->buffers[0][1] == 0.5, "virtual IO preserves IR samples");
	}
	_wputenv_s(L"EAPO_XT_JUDGED_IR", L"");
	DeleteFileW(filename.c_str());
}

TEST_CASE(HybridConvTests, assertIrReadDiagnosticsNameTheirFiles)
{
	const wstring unreadable = scratchDirectory().trackFile(L"not-an-ir.wav");
	HANDLE raw = CreateFileW(unreadable.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	harness.require(raw != INVALID_HANDLE_VALUE, "invalid IR fixture opens");
	const char invalid[] = "not an impulse response";
	DWORD written = 0;
	const bool wrote = WriteFile(raw, invalid, sizeof(invalid), &written, nullptr) != FALSE
		&& written == sizeof(invalid);
	CloseHandle(raw);
	harness.require(wrote, "invalid IR fixture is written");

	const wstring mismatch = scratchDirectory().trackFile(L"wrong-rate-ir.wav");
	harness.require(test::writeWavFile(mismatch, 44100, std::vector<double>{1.0}),
		"sample-rate mismatch IR fixture is written");
	const auto unreadableTarget = ConfigFileReference::target(L"", unreadable);
	const auto mismatchTarget = ConfigFileReference::target(L"", mismatch);
	harness.require(unreadableTarget && mismatchTarget, "IR diagnostic fixtures are judged");
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "IR diagnostic log opens");
	Logging::useStream(logFile, false, true, false);
	loadIrCached(unreadableTarget.path, sampleRate, true);
	loadIrCached(mismatchTarget.path, sampleRate, true);
	std::fflush(logFile);
	std::rewind(logFile);
	wstring log;
	wchar_t text[1024] = {};
	while (std::fgetws(text, 1024, logFile) != nullptr)
		log.append(text);
	Logging::useStream(stdout, true, true, false);
	std::fclose(logFile);
	harness.expectTrue(log.find(unreadable) != wstring::npos, "IR read error names its file");
	harness.expectTrue(log.find(mismatch) != wstring::npos, "IR sample-rate mismatch names its file");
}

vector<double> renderConvolutionFilter(const wstring& filename, int firstFrameLength)
{
	ConvolutionFilter filter(ConfigFileReference::target(L"", filename).path);
	vector<wstring> channels = {L"L"};
	filter.initialize(static_cast<float>(sampleRate), frameLength, channels);

	const int framesToRender = sampleRate / frameLength * 2 + 4;
	vector<double> inputStorage(frameLength, 0.0);
	vector<double> outputStorage(frameLength, 0.0);
	double* input[] = {inputStorage.data()};
	double* output[] = {outputStorage.data()};
	vector<double> rendered((size_t)framesToRender * frameLength, 0.0);

	fill(inputStorage.begin(), inputStorage.end(), 0.0);
	fill(outputStorage.begin(), outputStorage.end(), 0.0);
	filter.process(output, input, firstFrameLength);

	for (int frame = 0; frame < framesToRender; ++frame)
	{
		fill(inputStorage.begin(), inputStorage.end(), 0.0);
		fill(outputStorage.begin(), outputStorage.end(), 0.0);

		if (frame == 0)
			inputStorage[0] = 1.0;

		filter.process(output, input, frameLength);
		copy(outputStorage.begin(), outputStorage.end(), rendered.begin() + (size_t)frame * frameLength);
	}

	return rendered;
}

void assertSparseImpulseResponseSurvivesPastOneSecond(int leadingSilentFrames)
{
	vector<Tap> taps = {
		{0, 0.5},
		{frameLength - 1, -0.25},
		{sampleRate - 1, 0.125},
		{sampleRate, -0.0625},
		{sampleRate + frameLength + 17, 0.03125},
		{sampleRate * 2 + 239, -0.015625},
	};

	vector<double> impulseResponse((size_t)sampleRate * 2 + frameLength, 0.0);
	for (const Tap& tap : taps)
		impulseResponse[tap.sample] = tap.value;

	vector<double> rendered = renderImpulseResponse(impulseResponse, leadingSilentFrames);
	const int impulseStart = leadingSilentFrames * frameLength;

	for (const Tap& tap : taps)
		expectClose(rendered[(size_t)impulseStart + tap.sample], tap.value, impulseStart + tap.sample);
}

TEST_CASE(HybridConvTests, assertConvolutionFilterRecoversFromInitialShortFrame)
{
	vector<double> impulseResponse((size_t)sampleRate * 2 + frameLength, 0.0);
	impulseResponse[0] = 0.5;
	impulseResponse[sampleRate] = -0.125;
	impulseResponse[sampleRate + frameLength + 17] = 0.0625;

	wstring filename = createImpulseResponseFile(impulseResponse);
	vector<double> rendered = renderConvolutionFilter(filename, 128);
	DeleteFileW(filename.c_str());

	expectClose(rendered[0], impulseResponse[0], 0);
	expectClose(rendered[sampleRate], impulseResponse[sampleRate], sampleRate);
	expectClose(rendered[sampleRate + frameLength + 17], impulseResponse[sampleRate + frameLength + 17], sampleRate + frameLength + 17);
}

// Checks one plane-pointer array pair (real/imag) for the slab layout
// contract: every plane 64-byte aligned, real/imag adjacent at a constant
// offset that holds a full plane, and a constant partition stride of exactly
// two plane offsets, so a block's partition sweep walks one contiguous
// allocation instead of hundreds of scattered heap blocks.
void expectSlabLayout(const double* const* real, const double* const* imag, int count, int planeLength, const char* what)
{
	char message[128];

	if (count < 2)
		fail(string(what) + ": test needs at least two partitions");

	const ptrdiff_t planeOffset = imag[0] - real[0];
	snprintf(message, sizeof(message), "%s: imag plane must sit one padded plane after real (offset %td, plane %d)",
		what, planeOffset, planeLength);
	harness.expect(planeOffset >= planeLength, message);

	for (int s = 0; s < count; s++)
	{
		snprintf(message, sizeof(message), "%s: partition %d real plane is not 64-byte aligned", what, s);
		harness.expect(reinterpret_cast<uintptr_t>(real[s]) % 64 == 0, message);
		snprintf(message, sizeof(message), "%s: partition %d imag plane is not 64-byte aligned", what, s);
		harness.expect(reinterpret_cast<uintptr_t>(imag[s]) % 64 == 0, message);
		snprintf(message, sizeof(message), "%s: partition %d real/imag offset differs from partition 0", what, s);
		harness.expect(imag[s] - real[s] == planeOffset, message);
	}

	for (int s = 0; s + 1 < count; s++)
	{
		snprintf(message, sizeof(message), "%s: partition %d does not follow partition %d contiguously", what, s + 1, s);
		harness.expect(real[s + 1] - real[s] == 2 * planeOffset, message);
	}
}

// The per-block partition sweep in hcProcessSingle touches every filter plane
// and one mix plane per partition. With per-plane heap allocations that sweep
// crosses hundreds of scattered pages (DTLB pressure, prefetcher restarts);
// the layout contract pins the single-slab arrangement instead.
TEST_CASE(HybridConvTests, assertPartitionBuffersFormOneSlab)
{
	vector<double> impulseResponse((size_t)sampleRate * 2, 0.0);
	impulseResponse[0] = 1.0;
	impulseResponse[impulseResponse.size() - 1] = 0.5;

	HConvSingle filter = {};
	hcInitSingle(&filter, impulseResponse.data(), static_cast<int>(impulseResponse.size()), frameLength, 1);

	if (filter.num_filterbuf < 100)
		fail("slab layout test expected a partition count in the hundreds");

	expectSlabLayout(filter.filterbuf_freq_real, filter.filterbuf_freq_imag,
		filter.num_filterbuf, frameLength + 1, "filter planes");
	expectSlabLayout(filter.mixbuf_freq_real, filter.mixbuf_freq_imag,
		filter.num_mixbuf, frameLength + 1, "mix planes");

	hcCloseSingle(&filter);
}

TEST_CASE(HybridConvTests, assertEmptyConvolverCloseIsIdempotent)
{
	HConvSingle filter = {};
	hcCloseSingle(nullptr);
	hcCloseSingle(&filter);
	vector<double> impulseResponse(1, 1.0);
	hcInitSingle(&filter, impulseResponse.data(), 1, 8, 1);
	hcCloseSingle(&filter);
	hcCloseSingle(&filter);

	harness.expect(filter.storage == nullptr, "closing an empty convolver should preserve its empty state");
}

TEST_CASE(HybridConvTests, assertSharedFilterBankKeepsIndependentRuntimeState)
{
	vector<double> impulseResponse((size_t)frameLength * 2, 0.0);
	impulseResponse[0] = 0.5;
	impulseResponse[frameLength + 17] = -0.25;

	HConvSingle prototype = {};
	HConvSingle sibling = {};
	hcInitSingle(&prototype, impulseResponse.data(), static_cast<int>(impulseResponse.size()), frameLength, 1);
	hcInitSingleWithSharedFilterBank(&sibling, &prototype);

	harness.expectTrue(
		prototype.filterbuf_freq_real == sibling.filterbuf_freq_real &&
		prototype.filterbuf_freq_imag == sibling.filterbuf_freq_imag,
		"sibling channels should reuse one immutable transformed filter bank");
	harness.expectTrue(
		prototype.history_time != sibling.history_time &&
		prototype.mixbuf_freq_real != sibling.mixbuf_freq_real &&
		prototype.dft_time != sibling.dft_time,
		"sibling channels must retain independent mutable convolution state");

	// The sibling owns a shared reference, not a borrowed pointer. Destroying
	// the prototype first must leave every transformed partition alive.
	hcCloseSingle(&prototype);
	vector<double> input(frameLength, 0.0);
	vector<double> output(frameLength, 0.0);
	vector<double> rendered((size_t)frameLength * 3, 0.0);
	for (int frame = 0; frame < 3; ++frame)
	{
		fill(input.begin(), input.end(), 0.0);
		if (frame == 0)
			input[0] = 1.0;
		hcPutSingle(&sibling, input.data());
		hcProcessSingle(&sibling);
		hcGetSingle(&sibling, output.data());
		copy(output.begin(), output.end(), rendered.begin() + (size_t)frame * frameLength);
	}
	expectClose(rendered[0], 0.5, 0);
	expectClose(rendered[frameLength + 17], -0.25, frameLength + 17);
	hcCloseSingle(&sibling);
}

double nextRandom(unsigned& state)
{
	state = state * 1664525u + 1013904223u;
	return (static_cast<double>(state >> 8) / 8388608.0) - 1.0;
}

vector<double> renderPartitioned(const vector<double>& impulse, const vector<double>& input,
	int blockLength)
{
	HConvPartitioned filter = {};
	hcInitPartitioned(&filter, impulse.data(), static_cast<int>(impulse.size()), blockLength);
	const size_t outputLength = input.size() + impulse.size() - 1;
	const size_t blockCount = (outputLength + static_cast<size_t>(blockLength) - 1) / blockLength;
	vector<double> inputBlock(blockLength, 0.0);
	vector<double> outputBlock(blockLength, 0.0);
	vector<double> output(blockCount * static_cast<size_t>(blockLength), 0.0);
	for (size_t block = 0; block < blockCount; ++block)
	{
		fill(inputBlock.begin(), inputBlock.end(), 0.0);
		const size_t inputOffset = block * static_cast<size_t>(blockLength);
		const size_t copyCount = inputOffset < input.size()
			? (std::min)(static_cast<size_t>(blockLength), input.size() - inputOffset)
			: 0;
		if (copyCount != 0)
			std::copy_n(input.data() + inputOffset, copyCount, inputBlock.data());
		hcPutPartitioned(&filter, inputBlock.data());
		hcProcessPartitioned(&filter);
		hcGetPartitioned(&filter, outputBlock.data());
		copy(outputBlock.begin(), outputBlock.end(),
			output.begin() + block * static_cast<size_t>(blockLength));
	}
	hcClosePartitioned(&filter);
	output.resize(outputLength);
	return output;
}

double directConvolutionSample(const vector<double>& impulse, const vector<double>& input,
	size_t sample)
{
	const size_t firstInput = sample >= impulse.size() - 1 ? sample - (impulse.size() - 1) : 0;
	const size_t lastInput = (std::min)(sample, input.size() - 1);
	double result = 0.0;
	for (size_t i = firstInput; i <= lastInput; ++i)
		result += input[i] * impulse[sample - i];
	return result;
}

void assertPartitionedAgainstDirect(int impulseLength, int blockLength, int randomInputBlocks,
	bool checkEverySample)
{
	unsigned randomState = static_cast<unsigned>(impulseLength * 31 + blockLength);
	vector<double> impulse(static_cast<size_t>(impulseLength));
	const double impulseScale = 1.0 / sqrt(static_cast<double>(impulseLength));
	for (double& value : impulse)
		value = nextRandom(randomState) * impulseScale;
	vector<double> input(static_cast<size_t>(randomInputBlocks) * blockLength);
	for (double& value : input)
		value = nextRandom(randomState);

	const vector<double> actual = renderPartitioned(impulse, input, blockLength);
	vector<size_t> samples;
	if (checkEverySample)
	{
		samples.resize(actual.size());
		for (size_t i = 0; i < samples.size(); ++i)
			samples[i] = i;
	}
	else
	{
		const size_t blockCount = (actual.size() + static_cast<size_t>(blockLength) - 1) /
			static_cast<size_t>(blockLength);
		samples.reserve(blockCount * 2);
		for (size_t block = 0; block < blockCount; ++block)
		{
			const size_t first = block * static_cast<size_t>(blockLength);
			if (first < actual.size())
				samples.push_back(first);
			const size_t middle = first + static_cast<size_t>(blockLength / 2);
			if (middle < actual.size())
				samples.push_back(middle);
		}
	}

	double referenceEnergy = 0.0;
	double maximumError = 0.0;
	for (size_t sample : samples)
	{
		const double expected = directConvolutionSample(impulse, input, sample);
		const double error = abs(actual[sample] - expected);
		referenceEnergy += expected * expected;
		maximumError = (std::max)(maximumError, error);
	}
	const double referenceRms = sqrt(referenceEnergy / samples.size());
	// FFT roundoff grows with transform length and accumulated partitions. A
	// 1e-9 error relative to the direct-convolution RMS is well above that
	// roundoff while still rejecting timing, overlap and stale-ring errors.
	const double allowedError = 1.0e-11 + 1.0e-9 * referenceRms;
	char message[256];
	snprintf(message, sizeof(message),
		"partitioned direct comparison hlen=%d B=%d: max %.17g, allowance %.17g",
		impulseLength, blockLength, maximumError, allowedError);
	harness.expect(maximumError <= allowedError, message);
}

TEST_CASE(HybridConvTests, assertPartitionedConvolutionMatchesDirectConvolution)
{
	// These lengths cover the smallest eligible 2L boundary, its first tail
	// sample, a one-partition tail, and an exact multiple of L for B=128.
	assertPartitionedAgainstDirect(1000, 128, 24, true);
	assertPartitionedAgainstDirect(1024, 128, 24, true);
	assertPartitionedAgainstDirect(1025, 128, 24, true);
	assertPartitionedAgainstDirect(1536, 128, 24, true);
	assertPartitionedAgainstDirect(2048, 128, 24, true);

	// Long cases use two deterministic points per output block for the scalar
	// time-domain oracle, keeping the test bounded while crossing several K-block
	// input and output cycles with fully random IR and input samples.
	assertPartitionedAgainstDirect(96000, 480, 240, false);
	assertPartitionedAgainstDirect(70000, 441, 52, false);
	assertPartitionedAgainstDirect(70000, 960, 52, false);
	assertPartitionedAgainstDirect(96000, 128, 80, false);
}

TEST_CASE(HybridConvTests, assertPartitionedShortPathIsBitIdenticalToSingle)
{
	constexpr int shortBlockLength = 128;
	unsigned randomState = 0x51a71e5u;
	vector<double> impulse(1000);
	for (double& value : impulse)
		value = nextRandom(randomState);
	HConvSingle single = {};
	HConvPartitioned partitioned = {};
	hcInitSingle(&single, impulse.data(), static_cast<int>(impulse.size()), shortBlockLength, 1);
	hcInitPartitioned(&partitioned, impulse.data(), static_cast<int>(impulse.size()), shortBlockLength);
	harness.expect(partitioned.tailfactor == 0,
		"an IR shorter than the default 2L boundary should retain the Single path");
	vector<double> input(shortBlockLength);
	vector<double> singleOutput(shortBlockLength);
	vector<double> partitionedOutput(shortBlockLength);
	for (int block = 0; block < 40; ++block)
	{
		for (double& value : input)
			value = nextRandom(randomState);
		hcPutSingle(&single, input.data());
		hcProcessSingle(&single);
		hcGetSingle(&single, singleOutput.data());
		hcPutPartitioned(&partitioned, input.data());
		hcProcessPartitioned(&partitioned);
		hcGetPartitioned(&partitioned, partitionedOutput.data());
		harness.expect(memcmp(singleOutput.data(), partitionedOutput.data(),
			singleOutput.size() * sizeof(double)) == 0,
			"short partitioned output should be bit-identical to HConvSingle");
	}
	hcClosePartitioned(&partitioned);
	hcCloseSingle(&single);
}

TEST_CASE(HybridConvTests, assertPartitionedSharedBankAndInitialState)
{
	constexpr int blockLength = 128;
	constexpr int impulseLength = 24000;
	unsigned randomState = 0xa5515eedu;
	vector<double> impulse(impulseLength);
	for (double& value : impulse)
		value = nextRandom(randomState) / sqrt(static_cast<double>(impulseLength));
	HConvPartitioned prototype = {};
	HConvPartitioned sibling = {};
	HConvPartitioned independent = {};
	hcInitPartitioned(&prototype, impulse.data(), impulseLength, blockLength);
	hcInitPartitionedWithSharedFilterBank(&sibling, &prototype);
	hcInitPartitioned(&independent, impulse.data(), impulseLength, blockLength);
	harness.require(prototype.tailfactor > 0 && sibling.tailfactor == prototype.tailfactor,
		"long shared-bank fixtures should select the same two-stage shape");

	const int firstTwoTailLengths = 2 * prototype.tailfactor * blockLength;
	vector<double> zero(blockLength, 0.0);
	vector<double> prototypeOutput(blockLength, 1.0);
	vector<double> siblingOutput(blockLength, 1.0);
	vector<double> independentOutput(blockLength, 1.0);
	for (int sample = 0; sample < firstTwoTailLengths; sample += blockLength)
	{
		hcPutPartitioned(&prototype, zero.data());
		hcProcessPartitioned(&prototype);
		hcGetPartitioned(&prototype, prototypeOutput.data());
		harness.expect(std::all_of(prototypeOutput.begin(), prototypeOutput.end(),
			[](double value) { return value == 0.0; }),
			"the first 2L zero-input samples should contain no stale ring data");
	}

	// The sibling owns both immutable stages through shared references. Closing
	// the prototype must not invalidate either bank.
	hcClosePartitioned(&prototype);
	for (int block = 0; block < 80; ++block)
	{
		vector<double> input(blockLength);
		for (double& value : input)
			value = nextRandom(randomState);
		for (HConvPartitioned* convolver : {&sibling, &independent})
		{
			hcPutPartitioned(convolver, input.data());
			hcProcessPartitioned(convolver);
		}
		hcGetPartitioned(&sibling, siblingOutput.data());
		hcGetPartitioned(&independent, independentOutput.data());
		harness.expect(memcmp(siblingOutput.data(), independentOutput.data(),
			siblingOutput.size() * sizeof(double)) == 0,
			"a shared tail bank should match an independently transformed bank");
	}

	// Closing and rebuilding is the public reset sequence. The rebuilt instance
	// must return to the same zeroed state as a fresh convolver.
	hcClosePartitioned(&prototype);
	hcInitPartitioned(&prototype, impulse.data(), impulseLength, blockLength);
	hcPutPartitioned(&prototype, zero.data());
	hcProcessPartitioned(&prototype);
	hcGetPartitioned(&prototype, prototypeOutput.data());
	harness.expect(std::all_of(prototypeOutput.begin(), prototypeOutput.end(),
		[](double value) { return value == 0.0; }),
		"close and reinitialize should reset every partitioned runtime ring");

	hcClosePartitioned(&prototype);
	hcClosePartitioned(&sibling);
	hcClosePartitioned(&independent);
}

TEST_CASE(HybridConvTests, assertPartitionedBoundaryTapsFlushToSilence)
{
	constexpr int blockLength = 480;
	constexpr int impulseLength = 96000;
	HConvPartitioned filter = {};
	vector<double> impulse(impulseLength, 0.0);
	hcInitPartitioned(&filter, impulse.data(), impulseLength, blockLength);
	const int tailLength = filter.tailfactor * blockLength;
	const int tapSamples[] = {0, 2 * tailLength - 1, 2 * tailLength,
		3 * tailLength - 1, 3 * tailLength, impulseLength - 1};
	const double tapValues[] = {0.5, -0.25, 0.125, -0.0625, 0.03125, -0.015625};
	hcClosePartitioned(&filter);
	for (size_t i = 0; i < std::size(tapSamples); ++i)
		impulse[tapSamples[i]] = tapValues[i];
	hcInitPartitioned(&filter, impulse.data(), impulseLength, blockLength);

	const int convolutionBlocks =
		(impulseLength + blockLength - 1) / blockLength;
	const int silenceBlocks = 2 * filter.tailfactor + 4;
	vector<double> input(blockLength, 0.0);
	vector<double> output(blockLength, 0.0);
	vector<double> rendered(static_cast<size_t>(convolutionBlocks + silenceBlocks) * blockLength);
	for (int block = 0; block < convolutionBlocks + silenceBlocks; ++block)
	{
		fill(input.begin(), input.end(), 0.0);
		if (block == 0)
			input[0] = 1.0;
		hcPutPartitioned(&filter, input.data());
		hcProcessPartitioned(&filter);
		hcGetPartitioned(&filter, output.data());
		copy(output.begin(), output.end(),
			rendered.begin() + static_cast<size_t>(block) * blockLength);
	}
	for (size_t i = 0; i < std::size(tapSamples); ++i)
		harness.expectNear(rendered[tapSamples[i]], tapValues[i], 1.0e-11,
			"partition boundary tap " + std::to_string(tapSamples[i]));
	for (size_t sample = impulseLength; sample < rendered.size(); ++sample)
		harness.expect(abs(rendered[sample]) <= 1.0e-11,
			"partitioned convolution should flush to numerical silence");
	hcClosePartitioned(&filter);
	hcClosePartitioned(&filter);
}

TEST_CASE(HybridConvTests, assertInvalidConvolverArgumentsLeaveEmptyOutput)
{
	vector<double> impulseResponse(1, 1.0);
	HConvSingle filter = {};
	bool rejected = false;
	try
	{
		hcInitSingle(&filter, impulseResponse.data(), 1, 1, 0);
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}

	harness.expect(rejected, "hcInitSingle should reject a zero processing-step count");
	harness.expect(filter.storage == nullptr, "failed hcInitSingle should leave a safely closable empty output");
	hcCloseSingle(&filter);

	rejected = false;
	try
	{
		hcInitSingle(&filter, impulseResponse.data(), 1, (std::numeric_limits<int>::max)(), 1);
	}
	catch (const std::length_error&)
	{
		rejected = true;
	}
	harness.expect(rejected, "hcInitSingle should reject a frame length that overflows FFTW's transform size");

	HConvPartitioned partitioned = {};
	rejected = false;
	try
	{
		hcInitPartitioned(&partitioned, impulseResponse.data(), 1,
			(std::numeric_limits<int>::max)());
	}
	catch (const std::length_error&)
	{
		rejected = true;
	}
	harness.expect(rejected, "hcInitPartitioned should reject an overflowing frame length");
	harness.expect(partitioned.head.storage == nullptr && partitioned.storage == nullptr,
		"failed hcInitPartitioned should leave a safely closable empty output");
	hcClosePartitioned(&partitioned);
	hcClosePartitioned(&partitioned);
}

TEST_CASE(HybridConvTests, assertConvolverArraySupportsOutOfOrderInitialization)
{
	constexpr unsigned slotCount = 2;
	auto slots = AlignedMemory::allocateArray<HConvPartitioned>(slotCount);
	// cppcheck 2.21 reports a parser error on this ordinary null check after the
	// templated/static_cast allocation expression; MSVC builds and runs the path.
	// cppcheck-suppress syntaxError
	if (slots == nullptr)
		fail("could not allocate convolver slots for partial-initialization test");
	HConvPartitionedArray pending;
	pending.adoptStorage(std::move(slots), slotCount);
	harness.expect(pending[0].head.storage == nullptr && pending[1].head.storage == nullptr,
		"adopting convolver storage should establish inert slots");
	vector<double> impulseResponse(1, 1.0);
	// Initialize the later slot first. Parallel preparation is free to finish
	// slots in any order, and reset must close both this live slot and the
	// untouched zero slot safely.
	hcInitPartitioned(&pending[1], impulseResponse.data(), 1, 8);

	HConvPartitionedArray owner(std::move(pending));
	owner.reset();
	owner.reset();
}

TEST_CASE(HybridConvTests, assertConvolutionPathParsing)
{
	_wputenv_s(L"EAPO_XT_TEST_IR_DIR", L"C:\\Impulse Responses");

	harness.expectTrue(
		ConfigFileReference::normalize(L"  \"room with spaces.wav\"  ") == L"room with spaces.wav",
		"quoted convolution path was not normalized");
	harness.expectTrue(
		ConfigFileReference::normalize(L"%EAPO_XT_TEST_IR_DIR%\\room.wav") == L"C:\\Impulse Responses\\room.wav",
		"convolution path environment variable was not expanded");
	harness.expectTrue(
		ConfigFileReference::resolve(L"C:\\EqualizerAPO\\config\\config.txt", L"\"irs\\room.wav\"") == L"C:\\EqualizerAPO\\config\\irs\\room.wav",
		"relative convolution path was not resolved from the config file directory");
	harness.expectTrue(
		ConfigFileReference::resolve(L"C:\\EqualizerAPO\\config\\config.txt", L"") == L"",
		"empty convolution path should remain empty");

	// VSTPlugin's own rule, kept beside the shared one (audit #348 A1).
	harness.expectTrue(
		ConfigFileReference::resolveLibrary(L"C:\\VST\\", L"sub\\plug.dll") == L"C:\\VST\\sub\\plug.dll",
		"a relative plug-in reference is taken from the plug-in folder");
	harness.expectTrue(
		ConfigFileReference::resolveLibrary(L"C:\\VST", L"D:\\x\\plug.dll") == L"D:\\x\\plug.dll",
		"an absolute plug-in reference is kept as written");
	harness.expectTrue(
		ConfigFileReference::resolveLibrary(L"C:\\VST", L"\\x\\plug.dll") == L"\\x\\plug.dll",
		"a root-relative plug-in reference is not relative to the plug-in folder");
	harness.expectTrue(ConfigFileReference::resolveLibrary(L"C:\\VST", L"") == L"", "an empty plug-in reference stays empty");

	// target() is the pair every opening factory uses: resolved, then judged.
	const ConfigFileReference::Target local =
		ConfigFileReference::target(L"C:\\EqualizerAPO\\config\\config.txt", L"\"%EAPO_XT_TEST_IR_DIR%\\room.wav\"");
	harness.expectTrue(local.path.path() == L"C:\\Impulse Responses\\room.wav" && local.refusal.empty(),
		"a quoted local reference with a variable resolves and is allowed");
	const ConfigFileReference::Target share =
		ConfigFileReference::target(L"C:\\EqualizerAPO\\config\\config.txt", L"\\\\nas\\irs\\room.wav");
	harness.expectTrue(share.path.empty() && !share.refusal.empty(),
		"a refused reference carries the refusal and no path to open");
	const ConfigFileReference::Target none = ConfigFileReference::target(L"C:\\EqualizerAPO\\config\\config.txt", L"  ");
	harness.expectTrue(none.path.empty() && none.refusal.empty(), "an empty reference has neither");
}

}

TEST_CASE(HybridConvTests, testSparseImpulseResponseSurvivesPastOneSecond)
{
	assertSparseImpulseResponseSurvivesPastOneSecond(0);
	assertSparseImpulseResponseSurvivesPastOneSecond(137);
}

void cleanupHybridConvTests()
{
	cleanupFftwWisdomTest();
	scratchDirectory().removeAll();
}

int main()
{
	Logging::set(stdout, true, true, false);
	try
	{
		return test::runAll("HybridConvTests", {}, cleanupHybridConvTests);
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "HybridConvTests: unhandled exception: %s\n", error.what());
	}
	catch (...)
	{
		std::fprintf(stderr, "HybridConvTests: unhandled non-standard exception\n");
	}
	return EXIT_FAILURE;
}
