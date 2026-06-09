/*
	This file is part of EqualizerAPO-XT.

	Simple regression tests for the libHybridConv bridge. These tests are
	kept framework-free so they can run wherever the Visual Studio build runs.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <windows.h>
#include <sndfile.h>

#include "filters/ConvolutionFilter.h"
#include "filters/ConvolutionFilePath.h"
#include "helpers/LogHelper.h"
#include "libHybridConv-0.1.1/libHybridConv_eapo.h"
#include "Tests/TestHarness.h"

// Forward declarations for the additional suites that share this binary's
// main() (defined in CommonLogicTests.cpp, CopyCommandTests.cpp,
// DelayCommandTests.cpp, GraphicEQCommandTests.cpp, VSTPluginCommandTests.cpp,
// ParserTests.cpp and ParserPreampTests.cpp).
void runCommonLogicTests();
void runCopyCommandTests();
void runDelayCommandTests();
void runGraphicEQCommandTests();
void runVSTPluginCommandTests();
void runParserTests();
void runParserPreampTests();

using std::string;
using std::vector;
using std::wstring;

namespace
{
constexpr int frameLength = 480;
constexpr int sampleRate = 48000;
constexpr double tolerance = 1.0e-8;

test::Harness harness("HybridConvTests");

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

	SNDFILE* file = sf_wchar_open(filename.c_str(), SFM_WRITE, &info);
	if (file == nullptr)
		fail("could not create temporary impulse response file");

	sf_count_t written = sf_writef_double(file, impulseResponse.data(), (sf_count_t)impulseResponse.size());
	sf_close(file);

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

int main()
{
	LogHelper::set(stdout, true, true, false);

	assertSparseImpulseResponseSurvivesPastOneSecond(0);
	assertSparseImpulseResponseSurvivesPastOneSecond(137);
	assertConvolutionFilterRecoversFromInitialShortFrame();
	assertConvolutionPathParsing();

	// Pure-logic helper and parser-extension coverage that also lives in this
	// console binary (see CommonLogicTests.cpp, CopyCommandTests.cpp,
	// DelayCommandTests.cpp, GraphicEQCommandTests.cpp, VSTPluginCommandTests.cpp,
	// ParserTests.cpp and ParserPreampTests.cpp).
	runCommonLogicTests();
	runCopyCommandTests();
	runDelayCommandTests();
	runGraphicEQCommandTests();
	runVSTPluginCommandTests();
	runParserTests();
	runParserPreampTests();

	harness.report();
	return 0;
}
