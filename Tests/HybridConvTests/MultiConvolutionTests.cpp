/*
	This file is part of EqualizerAPO-XT.

	Unit tests for the multi-input synthesis convolution filter
	(MultiConvolutionFilter): several input channels convolved with a single
	multi-channel impulse response and summed into one output channel.
*/

#include <cmath>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <windows.h>
#include <sndfile.h>

#include "filters/MultiConvolutionCommand.h"
#include "filters/MultiConvolutionFilter.h"
#include "Tests/TestHarness.h"

using std::vector;
using std::wstring;

namespace
{
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

	SNDFILE* file = sf_wchar_open(filename.c_str(), SFM_WRITE, &info);
	if (file == nullptr)
		harness.fail("could not create temporary impulse response file");
	sf_writef_double(file, interleaved.data(), (sf_count_t)frames);
	sf_close(file);

	return filename;
}

// First tracer bullet: two selected inputs, each convolved with a delta impulse
// response (pass-through), must be summed into the single output channel. With
// L = 0.3 and R = 0.5 the output is 0.8 everywhere. This is exactly the routing
// the 1:1 ConvolutionFilter cannot do (it would leave the two inputs separate).
void assertTwoInputsSumIntoOneOutput()
{
	vector<double> delta(frameLength, 0.0);
	delta[0] = 1.0;
	wstring irFile = createMultiChannelIr({delta, delta});

	MultiConvolutionFilter filter(L"Mixed", irFile);
	vector<wstring> inChannels = {L"L", L"R"};
	vector<wstring> outChannels = filter.initialize((float)sampleRate, frameLength, inChannels);
	DeleteFileW(irFile.c_str());

	harness.expectEqual(outChannels.size(), (size_t)1, "filter should declare exactly one output channel");
	harness.expectTrue(!outChannels.empty() && outChannels[0] == L"Mixed", "output channel should be named Mixed");

	vector<double> inL(frameLength, 0.3);
	vector<double> inR(frameLength, 0.5);
	vector<double> out(frameLength, 0.0);
	double* input[] = {inL.data(), inR.data()};
	double* output[] = {out.data()};
	filter.process(output, input, frameLength);

	for (int f = 0; f < frameLength; f++)
		harness.expectTrue(fabs(out[f] - 0.8) <= tolerance, "two inputs should be summed into one output");
}

// Second slice: each input must be convolved with its OWN IR channel. Distinct
// IR gains (2x on channel 0, 3x on channel 1) with distinct inputs (0.1, 0.2)
// give 0.1*2 + 0.2*3 = 0.8; a swapped pairing would give 0.7. The delta test
// above cannot catch a swap because two identical deltas hide it.
void assertEachInputPairsWithItsOwnIrChannel()
{
	vector<double> ir0(frameLength, 0.0);
	ir0[0] = 2.0;
	vector<double> ir1(frameLength, 0.0);
	ir1[0] = 3.0;
	wstring irFile = createMultiChannelIr({ir0, ir1});

	MultiConvolutionFilter filter(L"Mixed", irFile);
	vector<wstring> inChannels = {L"L", L"R"};
	filter.initialize((float)sampleRate, frameLength, inChannels);
	DeleteFileW(irFile.c_str());

	vector<double> inL(frameLength, 0.1);
	vector<double> inR(frameLength, 0.2);
	vector<double> out(frameLength, 0.0);
	double* input[] = {inL.data(), inR.data()};
	double* output[] = {out.data()};
	filter.process(output, input, frameLength);

	for (int f = 0; f < frameLength; f++)
		harness.expectTrue(fabs(out[f] - 0.8) <= tolerance, "each input must pair with its own IR channel");
}

// Third slice: the "MultiConvolution:" config line parses into an output channel
// and an IR path. The channel is the first token; the rest (trimmed) is the path
// and keeps its inner spaces. A non-matching command or a line without a path is
// rejected.
void assertCommandParsesChannelAndPath()
{
	MultiConvolutionCommand cmd;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"Mixed room.wav", cmd), "valid MultiConvolution line parses");
	harness.expectTrue(cmd.outputChannel == L"Mixed", "output channel is the first token");
	harness.expectTrue(cmd.path == L"room.wav", "IR path is the remainder");

	MultiConvolutionCommand spaced;
	harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", L"LeftEar sub dir\\room ir.wav", spaced), "path with inner spaces parses");
	harness.expectTrue(spaced.outputChannel == L"LeftEar", "channel stops at the first space");
	harness.expectTrue(spaced.path == L"sub dir\\room ir.wav", "path keeps inner spaces");

	MultiConvolutionCommand rejected;
	harness.expectFalse(MultiConvolutionCommand::parse(L"Convolution", L"Mixed room.wav", rejected), "a non-MultiConvolution command is rejected");
	harness.expectFalse(MultiConvolutionCommand::parse(L"MultiConvolution", L"OnlyChannel", rejected), "a line without an IR path is rejected");
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
		first.outputChannel = c.channel;
		first.path = c.path;
		wstring serialized = first.serialize();

		MultiConvolutionCommand second;
		harness.expectTrue(MultiConvolutionCommand::parse(L"MultiConvolution", serialized, second), "serialized line re-parses");
		harness.expectTrue(second.outputChannel == first.outputChannel, "channel round-trips");
		harness.expectTrue(second.path == first.path, "path round-trips");
		harness.expectTrue(second.serialize() == serialized, "second serialization is identical");
	}
}
} // namespace

void runMultiConvolutionTests()
{
	assertTwoInputsSumIntoOneOutput();
	assertEachInputPairsWithItsOwnIrChannel();
	assertCommandParsesChannelAndPath();
	assertCommandSerializeRoundTrips();
	harness.report();
}
