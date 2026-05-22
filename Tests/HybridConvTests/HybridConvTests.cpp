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

#include "libHybridConv-0.1.1/libHybridConv_eapo.h"

using namespace std;

namespace
{
constexpr int frameLength = 480;
constexpr int sampleRate = 48000;
constexpr double tolerance = 1.0e-8;

struct Tap
{
	int sample;
	double value;
};

void fail(const string& message)
{
	fprintf(stderr, "HybridConvTests failed: %s\n", message.c_str());
	exit(1);
}

void expectClose(double actual, double expected, int sample)
{
	if (fabs(actual - expected) > tolerance)
	{
		char message[256];
		snprintf(message, sizeof(message), "sample %d expected %.12g, got %.12g", sample, expected, actual);
		fail(message);
	}
}

vector<double> renderImpulseResponse(const vector<double>& impulseResponse, int leadingSilentFrames)
{
	HConvSingle filter = {};
	hcInitSingle(&filter, const_cast<double*>(impulseResponse.data()), (int)impulseResponse.size(), frameLength, 1);

	const int framesToRender = leadingSilentFrames + (int)((impulseResponse.size() + frameLength - 1) / frameLength) + 3;
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
}

int main()
{
	assertSparseImpulseResponseSurvivesPastOneSecond(0);
	assertSparseImpulseResponseSurvivesPastOneSecond(137);

	printf("HybridConvTests passed\n");
	return 0;
}
