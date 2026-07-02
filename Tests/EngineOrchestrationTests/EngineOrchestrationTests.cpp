/*
	This file is part of EqualizerAPO-XT.

	Engine orchestration tests. Exercises the parts of FilterEngine that the
	audio regression suite cannot localize: channel-name resolution into
	filter in/out channel indices, Copy routing, and the crossfade transition
	state machine that runs when a new configuration is loaded while audio is
	processing. All configs are written to a temp directory at runtime, so the
	tests carry no data files and are deterministic.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>

#include "FilterEngine.h"
#include "helpers/LogHelper.h"
#include "Tests/TestHarness.h"

namespace
{

std::vector<std::wstring>& writtenConfigFiles()
{
	static std::vector<std::wstring> files;
	return files;
}

// Per-process directory so parallel runs on one machine cannot collide.
std::wstring testDirectory()
{
	wchar_t tempPath[MAX_PATH] = {};
	DWORD len = GetTempPathW(MAX_PATH, tempPath);
	std::wstring dir = (len > 0 && len < MAX_PATH) ? tempPath : L".\\";
	dir += L"EngineOrchestrationTests-" + std::to_wstring(GetCurrentProcessId());
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

void removeTestDirectory()
{
	for (const std::wstring& file : writtenConfigFiles())
		DeleteFileW(file.c_str());
	writtenConfigFiles().clear();
	RemoveDirectoryW(testDirectory().c_str());
}

std::wstring writeConfig(test::Harness& harness, const std::wstring& fileName, const std::string& content)
{
	std::wstring path = testDirectory() + L"\\" + fileName;
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream << content;
	stream.close();
	if (!stream)
		harness.fail("could not write temp config file");
	writtenConfigFiles().push_back(path);
	return path;
}

// Builds an engine the same way AudioRegressionTests does: no registry
// dependency, config loaded from the caller-supplied path.
void initializeEngine(FilterEngine& engine, unsigned sampleRate, unsigned channels, unsigned maxFrameCount, const std::wstring& configPath)
{
	const std::wstring deviceName = L"EngineOrchestrationTests";
	const std::wstring connectionName = L"File";
	const std::wstring deviceGuid = L"";
	engine.setDeviceInfo(false, true, deviceName, connectionName, deviceGuid, deviceName + L" " + connectionName);
	engine.initialize((float)sampleRate, channels, channels, channels, 0, maxFrameCount, configPath);
}

// Processes one block of interleaved stereo DC input and returns the output.
std::vector<float> processDcBlock(FilterEngine& engine, float left, float right, unsigned frames)
{
	std::vector<float> input((size_t)frames * 2);
	std::vector<float> output((size_t)frames * 2, 0.0f);
	for (unsigned i = 0; i < frames; i++)
	{
		input[(size_t)i * 2 + 0] = left;
		input[(size_t)i * 2 + 1] = right;
	}
	engine.process(output.data(), input.data(), frames);
	return output;
}

// Narrows a wide string to the active code page. Temp paths and config text are
// ASCII in these tests; this avoids the wchar_t->char narrowing warning (C4244)
// that the std::string(begin, end) shortcut raises.
std::string toNarrow(const std::wstring& w)
{
	if (w.empty())
		return std::string();
	int len = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)len, '\0');
	WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), &s[0], len, nullptr, nullptr);
	return s;
}

// Writes a 2-channel delta impulse response (each channel passes its input
// straight through) to the temp dir so a MultiConvolution line has a real file
// to load. Registered for cleanup like the configs.
std::wstring writeStereoDeltaIr(test::Harness& harness, const std::wstring& fileName)
{
	std::wstring path = testDirectory() + L"\\" + fileName;
	const int frames = 16;
	std::vector<double> interleaved((size_t)frames * 2, 0.0);
	interleaved[0] = 1.0; // channel 0, first sample
	interleaved[1] = 1.0; // channel 1, first sample

	SF_INFO info = {};
	info.samplerate = 48000;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;
	SNDFILE* file = sf_wchar_open(path.c_str(), SFM_WRITE, &info);
	if (file == nullptr)
		harness.fail("could not write stereo delta IR");
	sf_writef_double(file, interleaved.data(), frames);
	sf_close(file);

	writtenConfigFiles().push_back(path);
	return path;
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

// MultiConvolution must convolve every selected input with its own channel of a
// single multi-channel IR and SUM them into one output channel. With a stereo
// delta IR (pass-through) and inputs L=0.3, R=0.5, the "L" output becomes their
// sum 0.8, while R (not a filter output) passes through unchanged. This is the
// end-to-end check that both inputs are folded in, not just one.
void testMultiConvolutionSumsSelectedInputs(test::Harness& harness)
{
	std::wstring irPath = writeStereoDeltaIr(harness, L"mc_delta.wav");
	std::string irNarrow = toNarrow(irPath);

	std::wstring config = writeConfig(harness, L"multiconv.txt",
			"Channel: L R\n"
			"MultiConvolution: L \"" + irNarrow + "\"\n");

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, config);

	std::vector<float> output = processDcBlock(engine, 0.3f, 0.5f, 480);
	float left = output[(size_t)478 * 2 + 0];
	float right = output[(size_t)478 * 2 + 1];
	harness.expect(std::fabs(left - 0.8f) < 1e-3f, "L must be the summed convolution of both selected inputs");
	harness.expect(std::fabs(right - 0.5f) < 1e-3f, "R must pass through unchanged (it is not the filter's output)");
}

// Reads a stereo IR file into two channel-major buffers. Fails the harness if
// the file is missing or not stereo.
void readStereoIr(test::Harness& harness, const std::wstring& path, std::vector<double>& ch0, std::vector<double>& ch1)
{
	SF_INFO info = {};
	SNDFILE* file = sf_wchar_open(path.c_str(), SFM_READ, &info);
	if (file == nullptr)
		harness.fail("could not open BRIR IR file");
	if (info.channels != 2)
		harness.fail("BRIR IR file is not stereo");
	std::vector<double> interleaved((size_t)info.frames * info.channels);
	sf_readf_double(file, interleaved.data(), info.frames);
	sf_close(file);
	ch0.resize((size_t)info.frames);
	ch1.resize((size_t)info.frames);
	for (sf_count_t i = 0; i < info.frames; i++)
	{
		ch0[(size_t)i] = interleaved[(size_t)i * 2 + 0];
		ch1[(size_t)i] = interleaved[(size_t)i * 2 + 1];
	}
}

// Writes two channel-major buffers as a stereo double WAV, registered for cleanup.
void writeStereoIr(const std::wstring& path, const std::vector<double>& ch0, const std::vector<double>& ch1)
{
	size_t frames = std::min(ch0.size(), ch1.size());
	std::vector<double> interleaved(frames * 2);
	for (size_t i = 0; i < frames; i++)
	{
		interleaved[i * 2 + 0] = ch0[i];
		interleaved[i * 2 + 1] = ch1[i];
	}
	SF_INFO info = {};
	info.samplerate = 48000;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_DOUBLE;
	SNDFILE* file = sf_wchar_open(path.c_str(), SFM_WRITE, &info);
	sf_writef_double(file, interleaved.data(), (sf_count_t)frames);
	sf_close(file);
	writtenConfigFiles().push_back(path);
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
	if (dirBuf == nullptr)
	{
		std::printf("  [skip] testRealBrirCrossfeed: set EAPO_XT_BRIR_DIR to the BRIR folder to run it\n");
		return;
	}
	std::wstring dir(dirBuf);
	free(dirBuf);

	std::wstring flPath = dir + L"\\Thead400FL.wav";
	std::wstring frPath = dir + L"\\Thead400FR.wav";

	std::vector<double> flL, flR, frL, frR;
	readStereoIr(harness, flPath, flL, flR); // FL: ch0 = L-ear, ch1 = R-ear
	readStereoIr(harness, frPath, frL, frR); // FR: ch0 = L-ear, ch1 = R-ear

	// Ear-based IRs: Lear = both speakers -> left ear, Rear = both -> right ear.
	std::wstring lear = testDirectory() + L"\\Lear.wav";
	std::wstring rear = testDirectory() + L"\\Rear.wav";
	writeStereoIr(lear, flL, frL);
	writeStereoIr(rear, flR, frR);

	const unsigned frames = 480;
	const unsigned blocks = 80; // 38400 samples > IR length

	// Config A: full BRIR via two MultiConvolution filters.
	// Custom channel names must be upper-case: the "Channel:" command upper-cases
	// its selectors (ChannelCommand::parse), while "Copy:" keeps the target case
	// as written, so a mixed-case name would never match. SO/SE are private
	// scratch channels holding the original L/R before the ears overwrite L/R.
	std::string cfgA =
			"Copy: SO=L SE=R\n"
			"Channel: SO SE\n"
			"MultiConvolution: L \"" + toNarrow(lear) + "\"\n"
			"Channel: SO SE\n"
			"MultiConvolution: R \"" + toNarrow(rear) + "\"\n";
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

// Loading a new config while processing must crossfade smoothly from the old
// configuration to the new one (transitionLength = sampleRate / 100 samples),
// not step. Drives DC through a -6.0206 dB config, swaps to -20 dB, and
// checks continuity, bounds, progress, and convergence.
void testConfigSwapCrossfades(test::Harness& harness)
{
	const unsigned sampleRate = 48000;
	const unsigned transitionLength = sampleRate / 100; // matches FilterEngine::initialize
	const unsigned blockFrames = 120;

	std::wstring configA = writeConfig(harness, L"transition_a.txt", "Preamp: -6.0206 dB\n");
	std::wstring configB = writeConfig(harness, L"transition_b.txt", "Preamp: -20 dB\n");

	FilterEngine engine;
	initializeEngine(engine, sampleRate, 2, 480, configA);

	// Settle on config A.
	std::vector<float> settled = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(settled[0] - 0.5f) < 1e-3f, "engine did not settle on the initial config");

	// loadConfig with an existing currentConfig installs nextConfig and arms
	// the transition; process() then plays the crossfade.
	engine.loadConfig(configB);

	const float target = std::pow(10.0f, -20.0f / 20.0f); // 0.1
	std::vector<float> transition;
	for (unsigned block = 0; block * blockFrames < transitionLength; block++)
	{
		std::vector<float> output = processDcBlock(engine, 1.0f, 1.0f, blockFrames);
		for (unsigned i = 0; i < blockFrames; i++)
			transition.push_back(output[(size_t)i * 2]);
	}

	// Continuity: with a raised-cosine table over 480 samples and a 0.4 gain
	// span, adjacent samples may differ by ~0.0013; 0.01 leaves wide margin
	// while still failing hard on a step change.
	float previous = 0.5f;
	float maxDelta = 0.0f;
	for (float sample : transition)
	{
		maxDelta = std::max(maxDelta, std::fabs(sample - previous));
		previous = sample;
		harness.expect(sample <= 0.5f + 1e-3f && sample >= target - 1e-3f,
			"transition output left the [new gain, old gain] range");
	}
	harness.expect(maxDelta < 0.01f, "transition stepped instead of crossfading");

	// Progress: halfway through the transition the gain must sit strictly
	// between the two configs.
	float midway = transition[transition.size() / 2];
	harness.expect(midway < 0.49f && midway > 0.11f, "transition did not progress between the two configs");

	// Convergence: after the transition the new config is in sole control.
	std::vector<float> after = processDcBlock(engine, 1.0f, 1.0f, 480);
	float finalLeft = after[(size_t)478 * 2 + 0];
	float finalRight = after[(size_t)478 * 2 + 1];
	harness.expect(std::fabs(finalLeft - target) < 1e-4f, "left channel did not converge to the new config gain");
	harness.expect(std::fabs(finalRight - target) < 1e-4f, "right channel did not converge to the new config gain");
}

} // namespace

int main()
{
	LogHelper::set(stderr, false, false, false);

	test::Harness harness("EngineOrchestrationTests");

	testChannelSelectorRouting(harness);
	testCopySwapsChannels(harness);
	testMultiConvolutionSumsSelectedInputs(harness);
	testConfigSwapCrossfades(harness);
	testRealBrirCrossfeed(harness);

	removeTestDirectory();
	harness.report();
	return 0;
}
