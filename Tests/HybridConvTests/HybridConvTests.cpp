/*
	This file is part of EqualizerAPO-XT.

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
#include "filters/ConvolutionFilePath.h"
#include "filters/IrCache.h"
#include "services/logging/Logging.h"
#include "runtime/memory/AlignedMemory.h"
#include "audio/io/SndfileRAII.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"
#include "Tests/TestHarness.h"

// Forward declarations for the additional suites that share this binary's
// main(); each runXxxTests() is defined in the correspondingly named
// XxxTests.cpp next to this file.
void runAllPassTests();
void runSubwooferRoutingCodecTests();
void runSubwooferRoutingCommandTests();
void runSubwooferRoutingCompilerTests();
void runSubwooferRoutingEngineTests();
void runSubwooferRoutingVst3Tests();
void runSubwooferRoutingJsonTests();
void runSubwooferRoutingProcessorTests();
void runBiQuadKernelTests();
void runChannelCommandTests();
void runCommonLogicTests();
void runConvolutionCommandTests();
void runCopyCommandTests();
void runDelayCommandTests();
void runDeviceCommandTests();
void runExpressionCommandTests();
void runFilterFactoryRegistryTests();
void runGraphicEQCommandTests();
void runHilbertVelvetTests();
void runIfCommandTests();
void runIIRCommandTests();
void runIncludeCommandTests();
void runLoudnessCorrectionCommandTests();
void runStageCommandTests();
void runVSTPluginCommandTests();
void runVstHostTests();
void runVst3HostTests();
void runParserTests();
void runParserPreampTests();
void runMultiConvolutionTests();

using std::string;
using std::vector;
using std::wstring;

namespace
{
constexpr int frameLength = 480;
constexpr int sampleRate = 48000;
constexpr double tolerance = 1.0e-8;

test::Harness harness("HybridConvTests");
wstring wisdomTestDirectory;
wstring previousLocalAppData;

void assertFftwWisdomIsExported()
{
	wchar_t tempPath[MAX_PATH] = {};
	harness.expectTrue(GetTempPathW(MAX_PATH, tempPath) > 0,
		"FFTW wisdom test resolves temporary directory");
	wisdomTestDirectory = wstring(tempPath)
		+ L"EqualizerAPO-XT-wisdom-" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(wisdomTestDirectory.c_str(), nullptr);

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
	char message[256];
	snprintf(message, sizeof(message), "sample %d expected %.12g, got %.12g", sample, expected, actual);
	harness.expectTrue(fabs(actual - expected) <= tolerance, message);
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
	wchar_t tempPath[MAX_PATH] = {};
	wchar_t tempFile[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0)
		fail("GetTempPathW failed");
	if (GetTempFileNameW(tempPath, L"hc", 0, tempFile) == 0)
		fail("GetTempFileNameW failed");

	wstring filename = tempFile;
	DeleteFileW(filename.c_str());
	filename += L".wav";

	SF_INFO info = {};
	info.samplerate = sampleRate;
	info.channels = 1;
	info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;

	sndfile::Handle file(sf_wchar_open(filename.c_str(), SFM_WRITE, &info));
	if (!file)
		fail("could not create temporary impulse response file");

	sf_count_t written = sf_writef_double(file.get(), impulseResponse.data(), (sf_count_t)impulseResponse.size());

	if (written != (sf_count_t)impulseResponse.size())
		fail("could not write complete temporary impulse response file");

	return filename;
}

vector<double> renderConvolutionFilter(const wstring& filename, int firstFrameLength)
{
	ConvolutionFilter filter(filename);
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

void assertConvolutionFilterRecoversFromInitialShortFrame()
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
void assertPartitionBuffersFormOneSlab()
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

void assertEmptyConvolverCloseIsIdempotent()
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

void assertSharedFilterBankKeepsIndependentRuntimeState()
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

void assertInvalidConvolverArgumentsLeaveEmptyOutput()
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
}

void assertConvolverArraySupportsOutOfOrderInitialization()
{
	constexpr unsigned slotCount = 2;
	auto slots = AlignedMemory::allocateArray<HConvSingle>(slotCount);
	// cppcheck 2.21 reports a parser error on this ordinary null check after the
	// templated/static_cast allocation expression; MSVC builds and runs the path.
	// cppcheck-suppress syntaxError
	if (slots == nullptr)
		fail("could not allocate convolver slots for partial-initialization test");
	HConvSingleArray pending;
	pending.adoptStorage(std::move(slots), slotCount);
	harness.expect(pending[0].storage == nullptr && pending[1].storage == nullptr,
		"adopting convolver storage should establish inert slots");
	vector<double> impulseResponse(1, 1.0);
	// Initialize the later slot first. Parallel preparation is free to finish
	// slots in any order, and reset must close both this live slot and the
	// untouched zero slot safely.
	hcInitSingle(&pending[1], impulseResponse.data(), 1, 8, 1);

	HConvSingleArray owner(std::move(pending));
	owner.reset();
	owner.reset();
}

void assertConvolutionPathParsing()
{
	_wputenv_s(L"EAPO_XT_TEST_IR_DIR", L"C:\\Impulse Responses");

	harness.expectTrue(
		ConvolutionFilePath::normalizeParameter(L"  \"room with spaces.wav\"  ") == L"room with spaces.wav",
		"quoted convolution path was not normalized");
	harness.expectTrue(
		ConvolutionFilePath::normalizeParameter(L"%EAPO_XT_TEST_IR_DIR%\\room.wav") == L"C:\\Impulse Responses\\room.wav",
		"convolution path environment variable was not expanded");
	harness.expectTrue(
		ConvolutionFilePath::resolve(L"C:\\EqualizerAPO\\config\\config.txt", L"\"irs\\room.wav\"") == L"C:\\EqualizerAPO\\config\\irs\\room.wav",
		"relative convolution path was not resolved from the config file directory");
	harness.expectTrue(
		ConvolutionFilePath::resolve(L"C:\\EqualizerAPO\\config\\config.txt", L"") == L"",
		"empty convolution path should remain empty");
}

}

int runHybridConvTests()
{
	Logging::set(stdout, true, true, false);

	assertFftwWisdomIsExported();
	assertSparseImpulseResponseSurvivesPastOneSecond(0);
	assertSparseImpulseResponseSurvivesPastOneSecond(137);
	assertConvolutionFilterRecoversFromInitialShortFrame();
	assertPartitionBuffersFormOneSlab();
	assertSharedFilterBankKeepsIndependentRuntimeState();
	assertEmptyConvolverCloseIsIdempotent();
	assertInvalidConvolverArgumentsLeaveEmptyOutput();
	assertConvolverArraySupportsOutOfOrderInitialization();
	assertConvolutionPathParsing();

	// Pure-logic helper, command-codec and parser-extension coverage that also
	// lives in this console binary (one XxxTests.cpp per suite).
	// All-pass DSP invariants plus the characterizations the reform in issue
	// #228 has to preserve or is about to change deliberately.
	runAllPassTests();
	// Subwoofer Routing shared core (MIT SubwooferRoutingCore) plus its JSON codec:
	// pure-logic suites, no engine or device dependency.
	runSubwooferRoutingJsonTests();
	runSubwooferRoutingCodecTests();
	runSubwooferRoutingCommandTests();
	runSubwooferRoutingCompilerTests();
	// Engine-level integration: original issue #246 chain vs the preset,
	// Profile-form equivalence, invalid-state pass-through.
	runSubwooferRoutingEngineTests();
	runSubwooferRoutingProcessorTests();
	runBiQuadKernelTests();
	runChannelCommandTests();
	runCommonLogicTests();
	runConvolutionCommandTests();
	runCopyCommandTests();
	runDelayCommandTests();
	runDeviceCommandTests();
	runExpressionCommandTests();
	// Command-classification contract shared by the engine and the Editor
	// (FilterFactoryRegistry::canonicalCommand and the two keyword sets).
	runFilterFactoryRegistryTests();
	runGraphicEQCommandTests();
	runHilbertVelvetTests();
	runIfCommandTests();
	runIIRCommandTests();
	runIncludeCommandTests();
	runLoudnessCorrectionCommandTests();
	runStageCommandTests();
	runVSTPluginCommandTests();
	// Runtime VST2 host load/state/audio test. Soft-skips if the
	// companion TestVst2Plugin.dll is not next to this executable.
	runVstHostTests();
	// Runtime VST3 host contract test against the deterministic companion
	// module (bundle resolution, module lifecycle, host services, editor,
	// state, parameter flush). Unlike the VST2 suite above this one does not
	// soft-skip: a missing module fails the run, because the module is built
	// from this repository and its absence is a build problem.
	runVst3HostTests();
	// Native/VST3 parity for the Subwoofer Routing plugin, loaded through the
	// same host path as the companion-module suites above.
	runSubwooferRoutingVst3Tests();
	runParserTests();
	runParserPreampTests();
	runMultiConvolutionTests();

	cleanupFftwWisdomTest();
	harness.report();
	return 0;
}

int main()
{
	try
	{
		return runHybridConvTests();
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
