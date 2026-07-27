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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <future>
#include <memory>
#include <new>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "devices/DeviceAPOInfo.h"
#include "devices/DeviceAPOInfoKeys.h"
#include "engine/ConfigLoadTrace.h"
#include "engine/FilterEngine.h"
#include "engine/ConfigSwapChannel.h"
#include "engine/ConfigWatcher.h"
#include "helpers/ComBoundary.h"
#include "helpers/LogHelper.h"
#include "helpers/ParallelExecutor.h"
#include "helpers/RegistryHelper.h"
#include "helpers/SndfileRAII.h"
#include "helpers/SynchronizedState.h"
#include "helpers/Win32Event.h"
#include "Tests/TestHarness.h"

namespace
{
void testDeviceApoRegistryVocabulary(test::Harness& harness)
{
	harness.expectEqual(allGuidValueNameCount, 5u,
		"the five legacy APO slots stay in their indexed order");
	const auto ownedBegin = std::begin(ownedFxValueNames);
	const auto ownedEnd = std::end(ownedFxValueNames);
	for (const wchar_t* valueName : allGuidValueNames)
		harness.expect(std::find(ownedBegin, ownedEnd, valueName) != ownedEnd,
			"every installed APO GUID value is in the uninstall ownership table");
	for (const wchar_t* valueName : {
		sfxProcessingModesValueName, mfxProcessingModesValueName,
		efxProcessingModesValueName, fxTitleValueName })
	{
		harness.expect(std::find(ownedBegin, ownedEnd, valueName) != ownedEnd,
			"every installed processing value is in the uninstall ownership table");
	}
}

void testInstallStateComparisonIgnoresPadding(test::Harness& harness)
{
	using InstallState = DeviceAPOInfo::InstallState;
	alignas(InstallState) unsigned char leftStorage[sizeof(InstallState)];
	alignas(InstallState) unsigned char rightStorage[sizeof(InstallState)];
	std::fill_n(leftStorage, sizeof(leftStorage), static_cast<unsigned char>(0xAA));
	std::fill_n(rightStorage, sizeof(rightStorage), static_cast<unsigned char>(0x55));
	InstallState* left = new (leftStorage) InstallState();
	InstallState* right = new (rightStorage) InstallState();

	harness.expect(!(*left != *right),
		"logically identical install states ignore padding bytes");
	right->allowSilentBufferModification = true;
	harness.expect(*left != *right,
		"a changed install-state field is detected");

	left->~InstallState();
	right->~InstallState();
}

void testParallelExecutor(test::Harness& harness)
{
	constexpr size_t taskCount = 257;
	std::vector<std::atomic<unsigned>> visits(taskCount);
	for (std::atomic<unsigned>& visit : visits)
		visit.store(0, std::memory_order_relaxed);

	ParallelExecutor::forEach(taskCount, [&](size_t index) {
		visits[index].fetch_add(1, std::memory_order_relaxed);
	}, 4);
	for (size_t index = 0; index < taskCount; ++index)
		harness.expectEqual(visits[index].load(std::memory_order_relaxed), 1u,
			"parallel executor visits each task exactly once");

	bool propagated = false;
	try
	{
		ParallelExecutor::forEach(64, [](size_t index) {
			if (index == 7)
				throw std::runtime_error("parallel operation failed");
		}, 4);
	}
	catch (const std::runtime_error& error)
	{
		propagated = std::string(error.what()) == "parallel operation failed";
	}
	harness.expect(propagated, "parallel executor joins workers and propagates the first exception");
}

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

void testLogHelperFileDestination(test::Harness& harness)
{
	const std::wstring path = testDirectory() + L"\\LogHelperDestination.log";
	DeleteFileW(path.c_str());

	LogHelper::useFile(path, true, false, false);
	LogFStatic(L"file destination %d", 42);

	std::ifstream stream(path, std::ios::binary);
	const std::string contents((std::istreambuf_iterator<char>(stream)),
		std::istreambuf_iterator<char>());
	harness.expect(stream.good() || stream.eof(), "file logger writes a readable destination");
	harness.expect(contents.find("file destination 42") != std::string::npos,
		"file logger writes diagnostics to the selected path");

	LogHelper::useStream(stderr, false, false, false);
	DeleteFileW(path.c_str());
}

void testLogHelperUserDestination(test::Harness& harness)
{
	wchar_t previousLocalAppData[MAX_PATH] = {};
	const DWORD previousLength = GetEnvironmentVariableW(
		L"LOCALAPPDATA", previousLocalAppData, MAX_PATH);
	const std::wstring localRoot = testDirectory() + L"\\LocalAppData";
	CreateDirectoryW(localRoot.c_str(), nullptr);
	SetEnvironmentVariableW(L"LOCALAPPDATA", localRoot.c_str());

	harness.expect(LogHelper::useUserFile(L"Editor.log", true, false, false),
		"user logger creates its product log directory");
	LogFStatic(L"user destination");
	const std::wstring path = localRoot + L"\\EqualizerAPO\\logs\\Editor.log";
	std::ifstream stream(path, std::ios::binary);
	const std::string contents((std::istreambuf_iterator<char>(stream)),
		std::istreambuf_iterator<char>());
	harness.expect(contents.find("user destination") != std::string::npos,
		"user logger writes to the per-user Editor log");

	LogHelper::useStream(stderr, false, false, false);
	if (previousLength > 0 && previousLength < MAX_PATH)
		SetEnvironmentVariableW(L"LOCALAPPDATA", previousLocalAppData);
	else
		SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
	DeleteFileW(path.c_str());
	RemoveDirectoryW((localRoot + L"\\EqualizerAPO\\logs").c_str());
	RemoveDirectoryW((localRoot + L"\\EqualizerAPO").c_str());
	RemoveDirectoryW(localRoot.c_str());
}

void testRegistryExportHeaderPreservesQualifiedRoot(test::Harness& harness)
{
	const std::wstring key = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor\\Device\\FxProperties";
	harness.expect(RegistryHelper::formatExportHeader(key)
			== L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Vendor\\Device\\FxProperties]",
		"registry export writes an already-qualified key exactly once");
}

void testSynchronizedStateSerializesReplacement(test::Harness& harness)
{
	SynchronizedState<int> state(1);
	std::promise<void> enteredPromise;
	std::future<void> entered = enteredPromise.get_future();
	std::promise<void> releasePromise;
	std::shared_future<void> release = releasePromise.get_future().share();

	std::future<int> reader = std::async(std::launch::async, [&]() {
		return state.withLock([&](const int& value) {
			enteredPromise.set_value();
			release.wait();
			return value;
		});
	});
	harness.expect(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
		"synchronized state reader acquires the state");

	std::future<void> replacement = std::async(std::launch::async, [&]() {
		state.replace(2);
	});
	harness.expect(replacement.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
		"state replacement waits for an active reader");
	releasePromise.set_value();
	reader.wait();
	replacement.wait();
	harness.expectEqual(reader.get(), 1, "active reader keeps the original state alive");
	replacement.get();
	harness.expectEqual(state.withLock([](const int& value) { return value; }), 2,
		"subsequent reader observes the complete replacement");
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
	sndfile::Handle file(sf_wchar_open(path.c_str(), SFM_WRITE, &info));
	if (!file)
		harness.fail("could not write stereo delta IR");
	sf_writef_double(file.get(), interleaved.data(), frames);

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

// MultiConvolution must convolve the mapping target's OWN signal with its
// listed IR channels, independent of the Channel selection. The config selects
// only R before the line; under the old selection-based semantics that would
// change the result, under the mapping semantics it must not. With a stereo
// delta IR (pass-through) and L = 0.3, "L=0+1" folds L's own signal in twice
// (0.6), while R (not a mapping target) passes through unchanged.
void testMultiConvolutionIgnoresChannelSelection(test::Harness& harness)
{
	std::wstring irPath = writeStereoDeltaIr(harness, L"mc_delta.wav");
	std::string irNarrow = toNarrow(irPath);

	std::wstring config = writeConfig(harness, L"multiconv.txt",
			"Channel: R\n"
			"MultiConvolution: L=0+1 \"" + irNarrow + "\"\n");

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, config);

	std::vector<float> output = processDcBlock(engine, 0.3f, 0.5f, 480);
	float left = output[(size_t)478 * 2 + 0];
	float right = output[(size_t)478 * 2 + 1];
	harness.expect(std::fabs(left - 0.6f) < 1e-3f, "L must be its own signal convolved with both mapped IR channels");
	harness.expect(std::fabs(right - 0.5f) < 1e-3f, "R must pass through unchanged (it is not a mapping target)");
}

// Reads a stereo IR file into two channel-major buffers. Fails the harness if
// the file is missing or not stereo.
void readStereoIr(test::Harness& harness, const std::wstring& path, std::vector<double>& ch0, std::vector<double>& ch1)
{
	SF_INFO info = {};
	sndfile::Handle file(sf_wchar_open(path.c_str(), SFM_READ, &info));
	if (!file)
		harness.fail("could not open BRIR IR file");
	if (info.channels != 2)
		harness.fail("BRIR IR file is not stereo");
	std::vector<double> interleaved((size_t)info.frames * info.channels);
	sf_readf_double(file.get(), interleaved.data(), info.frames);
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
	sndfile::Handle file(sf_wchar_open(path.c_str(), SFM_WRITE, &info));
	sf_writef_double(file.get(), interleaved.data(), (sf_count_t)frames);
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
	std::unique_ptr<wchar_t, decltype(&std::free)> dirOwner(dirBuf, &std::free);
	if (!dirOwner)
	{
		std::printf("  [skip] testRealBrirCrossfeed: set EAPO_XT_BRIR_DIR to the BRIR folder to run it\n");
		return;
	}
	std::wstring dir(dirOwner.get());

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

	// Config A: full BRIR via the mapping form. Each mapping convolves its
	// target's own signal, so the speaker feeds are first copied into scratch
	// channels (SO/SE for the left ear, TO/TE for the right), convolved in
	// place against their ear IR channel, then summed into the real ears.
	std::string cfgA =
			"Copy: SO=L SE=R TO=L TE=R\n"
			"MultiConvolution: SO=0 SE=1 \"" + toNarrow(lear) + "\"\n"
			"MultiConvolution: TO=0 TE=1 \"" + toNarrow(rear) + "\"\n"
			"Copy: L=SO+SE R=TO+TE\n";
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
	const unsigned blockFrames = 120;

	std::wstring configA = writeConfig(harness, L"transition_a.txt", "Preamp: -6.0206 dB\n");
	std::wstring configB = writeConfig(harness, L"transition_b.txt", "Preamp: -20 dB\n");

	FilterEngine engine;
	initializeEngine(engine, sampleRate, 2, 480, configA);

	// The engine's real crossfade length; do not re-derive its formula here.
	// A zero length would leave the sampled transition empty, so the midpoint
	// read below needs this as a gating check.
	const unsigned transitionLength = engine.getTransitionLength();
	harness.require(transitionLength > 0, "engine reported no transition length after initialize");

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

// A reload is one transaction: filters may have been constructed and
// initialized, but the active configuration must not change unless the final
// FilterConfiguration allocation also succeeds. This injects failure at that
// exact allocation (one PreampFilter allocation succeeds first).
void testFailedConfigLoadKeepsActiveConfiguration(test::Harness& harness)
{
	std::wstring configA = writeConfig(harness, L"transaction_a.txt", "Preamp: -6.0206 dB\n");
	std::wstring configB = writeConfig(harness, L"transaction_b.txt", "Preamp: -20 dB\n");

	FilterEngine engine;
	initializeEngine(engine, 48000, 2, 480, configA);
	std::vector<float> before = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(before[0] - 0.5f) < 1e-3f, "transaction test did not establish config A");

	MemoryHelper::failAllocationAfterForTesting(1);
	bool loaded = engine.loadConfig(configB);
	MemoryHelper::resetAllocationFailureForTesting();
	harness.expectFalse(loaded, "reload reported success after injected FilterConfiguration allocation failure");

	std::vector<float> after = processDcBlock(engine, 1.0f, 1.0f, 480);
	harness.expect(std::fabs(after[0] - 0.5f) < 1e-3f,
		"failed reload replaced or damaged the active configuration");
}

// process() can arrive before initialize(), and initialize() can finish
// without loading any configuration (unreadable ConfigPath and no custom
// path). Both leave currentConfig null; every overload must pass audio
// through instead of dereferencing it inside audiodg.exe.
void testProcessWithoutConfigurationDoesNotCrash(test::Harness& harness)
{
	FilterEngine engine;

	const unsigned frames = 16;
	std::vector<float> inputF((size_t)2 * frames, 0.25f);
	std::vector<float> outputF((size_t)2 * frames, -1.0f);
	engine.process(outputF.data(), inputF.data(), frames);

	std::vector<double> inputD((size_t)2 * frames, 0.25);
	std::vector<double> outputD((size_t)2 * frames, -1.0);
	engine.process(outputD.data(), inputD.data(), frames);

	std::vector<float> planarFIn((size_t)2 * frames, 0.25f);
	std::vector<float> planarFOut((size_t)2 * frames, -1.0f);
	float* inF[2] = {planarFIn.data(), planarFIn.data() + frames};
	float* outF[2] = {planarFOut.data(), planarFOut.data() + frames};
	engine.process(outF, inF, frames);

	std::vector<double> planarDIn((size_t)2 * frames, 0.25);
	std::vector<double> planarDOut((size_t)2 * frames, -1.0);
	double* inD[2] = {planarDIn.data(), planarDIn.data() + frames};
	double* outD[2] = {planarDOut.data(), planarDOut.data() + frames};
	engine.process(outD, inD, frames);

	// Reaching this line is the point: no null dereference. Before
	// initialize() the channel counts are zero, so the bypass copies nothing
	// and the output buffers stay untouched.
	harness.expect(outputF[0] == -1.0f && outputD[0] == -1.0 && planarFOut[0] == -1.0f && planarDOut[0] == -1.0,
		"process() without a configuration wrote output despite zero channel counts");
}

void testConfigWatcherBackoffAndPathRefresh(test::Harness& harness)
{
	const std::wstring firstDirectory = testDirectory() + L"\\watch-a";
	const std::wstring secondDirectory = testDirectory() + L"\\watch-b";
	CreateDirectoryW(firstDirectory.c_str(), nullptr);
	CreateDirectoryW(secondDirectory.c_str(), nullptr);

	std::atomic<int> selectedPath = 0;
	std::atomic<int> snapshotCount = 0;
	std::atomic<int> callbackCount = 0;
	Win32Event shutdown(true, false);
	Win32Event changed(true, false);
	ConfigWatcher watcher(
		shutdown.get(),
		[&] {
			++snapshotCount;
			ConfigWatcher::Snapshot snapshot;
			if (selectedPath == 0)
				snapshot.directory = testDirectory() + L"\\missing-watch";
			else if (selectedPath == 1)
				snapshot.directory = firstDirectory;
			else
				snapshot.directory = secondDirectory;
			return snapshot;
		},
		[&] {
			++callbackCount;
			changed.set();
			return true;
		});
	std::thread worker([&] { watcher.run(); });

	Sleep(120);
	harness.expect(snapshotCount.load() <= 2,
		"unavailable config watch uses backoff instead of hot-looping");

	auto triggerAndWait = [&](int pathIndex, const std::wstring& directory,
		const char* label) {
		selectedPath = pathIndex;
		changed.reset();
		bool observed = false;
		for (int attempt = 0; attempt < 24 && !observed; ++attempt)
		{
			const std::wstring file = directory + L"\\change-"
				+ std::to_wstring(attempt) + L".txt";
			HANDLE output = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (output != INVALID_HANDLE_VALUE)
				CloseHandle(output);
			observed = WaitForSingleObject(changed.get(), 150) == WAIT_OBJECT_0;
			DeleteFileW(file.c_str());
		}
		harness.expect(observed, label);
	};

	triggerAndWait(1, firstDirectory,
		"watcher recovers when the config directory becomes available");
	triggerAndWait(2, secondDirectory,
		"watcher follows a ConfigPath directory change");

	shutdown.set();
	worker.join();
	RemoveDirectoryW(firstDirectory.c_str());
	RemoveDirectoryW(secondDirectory.c_str());
	harness.expect(callbackCount.load() >= 2,
		"both config directories produced change callbacks");
}

// initialize() seeds an empty active configuration and publishes the requested
// file through the same worker->RT channel used by every later reload. Before
// the first audio block consumes it, the public state query must conservatively
// report the pending transition instead of treating the config as directly
// installed.
void testInitialLoadUsesPublicationChannel(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"initial-publication.txt",
		"Preamp: -6.0206 dB\n");

	FilterEngine engine;
	const std::wstring deviceName = L"EngineOrchestrationTests";
	engine.setDeviceInfo(false, true, deviceName, L"File", L"", deviceName + L" File");
	engine.initialize(48000.0f, 2, 2, 2, 0, 16, configPath);

	harness.expect(engine.hasStatefulOrTailFilters(),
		"initial configuration bypassed the worker-to-RT publication channel");
}

void testConfigSwapChannelPermitRoundTrip(test::Harness& harness)
{
	using TestChannel = ConfigSwapChannel<std::unique_ptr<int>>;

	TestChannel channel;
	channel.reset(std::make_unique<int>(1));
	harness.require(channel.acquirePublishPermit(0),
		"fresh config channel did not grant its producer permit");
	channel.publish(std::make_unique<int>(2));
	harness.expect(channel.hasPending(), "published config was not visible to the RT side");
	harness.expectEqual(*channel.current(), 1, "publish replaced current before RT acquisition");

	channel.completeTransition();
	harness.expectFalse(channel.hasPending(), "completed transition stayed pending");
	harness.expectEqual(*channel.current(), 2, "completed transition did not promote pending config");
	harness.require(channel.acquirePublishPermit(0),
		"RT completion did not return the producer permit");
	channel.releasePublishPermit();

	harness.require(channel.acquirePublishPermit(0),
		"config channel did not grant a permit before reset");
	channel.publish(std::make_unique<int>(3));
	channel.reset(std::make_unique<int>(4));
	harness.expectFalse(channel.hasPending(), "reset kept a discarded pending config");
	harness.expectEqual(*channel.current(), 4, "reset did not install its seed config");
	harness.require(channel.acquirePublishPermit(0),
		"reset discarded a pending config without returning its permit");
	channel.releasePublishPermit();
}

// Windows may give a render APO fewer input channels than the endpoint output
// layout (for example, a stereo application stream feeding an 8-channel
// endpoint). An empty configuration is still responsible for adapting that
// connection: preserve the real input channels and silence the extra outputs.
void testEmptyConfigurationExpandsRenderChannels(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"empty-channel-expansion.txt",
		"# All filters are disabled\n");

	FilterEngine engine;
	const std::wstring deviceName = L"EngineOrchestrationTests";
	engine.setDeviceInfo(false, true, deviceName, L"File", L"", deviceName + L" File");
	engine.initialize(48000.0f, 2, 2, 8, 0, 16, configPath);

	constexpr unsigned frames = 4;
	float input[frames * 2] = {
		0.25f, -0.5f,
		0.5f, -0.25f,
		0.75f, 0.125f,
		1.0f, 0.0f
	};
	auto expectExpanded = [&](const float* output, const std::string& layout) {
		for (unsigned frame = 0; frame < frames; ++frame)
		{
			harness.expectEqual(output[frame * 8], input[frame * 2],
				layout + " preserves the left input channel while expanding");
			harness.expectEqual(output[frame * 8 + 1], input[frame * 2 + 1],
				layout + " preserves the right input channel while expanding");
			for (unsigned channel = 2; channel < 8; ++channel)
				harness.expectEqual(output[frame * 8 + channel], 0.0f,
					layout + " silences an added output channel");
		}
	};

	float output[frames * 8];
	std::fill_n(output, frames * 8, -1.0f);
	engine.process(output, input, frames);
	expectExpanded(output, "distinct-buffer empty config");

	float inPlace[frames * 8];
	std::fill_n(inPlace, frames * 8, -1.0f);
	std::copy_n(input, frames * 2, inPlace);
	engine.process(inPlace, inPlace, frames);
	expectExpanded(inPlace, "in-place empty config");
}

// The engine reports per-line facts while loading
// (branch decisions, Eval values, swallowed lines) through an attached
// ConfigLoadTraceSink so the Editor can echo them next to the config rows.
// This pins the whole grammar over one representative config: an Eval, a
// taken If with a nested false If inside, a short-circuited ElseIf, a dead
// Else, inline `expression` substitution, and file/line stamping.
void testConfigLoadTrace(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"trace.txt",
		"Eval: x = 2 + 3\n"          // line 1: Eval -> "5"
		"If: x == 5\n"               // line 2: Condition true
		"Preamp: -3 dB\n"            // line 3: executes, no entry
		"If: x > 100\n"              // line 4: Condition false
		"Delay: 1 ms\n"              // line 5: SkippedLine
		"EndIf:\n"                   // line 6: closes nested scope, no entry
		"ElseIf: x == 4\n"           // line 7: chain satisfied -> NotEvaluated
		"Preamp: -1 dB\n"            // line 8: SkippedLine
		"Else:\n"                    // line 9: ElseBranch, inactive
		"Preamp: -2 dB\n"            // line 10: SkippedLine
		"EndIf:\n"                   // line 11: closes outer scope, no entry
		"Preamp: `x - 10` dB\n");    // line 12: InlineValue "-5 dB"

	struct Collector : ConfigLoadTraceSink
	{
		std::vector<ConfigLoadTraceEntry> entries;
		void addEntry(const ConfigLoadTraceEntry& entry) override
		{
			entries.push_back(entry);
		}
	};
	Collector collector;

	FilterEngine engine;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 512, configPath);

	const std::vector<ConfigLoadTraceEntry>& entries = collector.entries;
	harness.requireEqual((int)entries.size(), 9, "load trace entry count");

	auto expectEntry = [&](int index, int line, ConfigLoadTraceEntry::Kind kind,
		ConfigLoadTraceEntry::Result result, bool active, const std::string& what) {
		const ConfigLoadTraceEntry& entry = entries[(size_t)index];
		harness.expectEqual(entry.line, line, what + ": line");
		harness.expect(entry.kind == kind, what + ": kind");
		harness.expect(entry.result == result, what + ": result");
		harness.expectEqual(entry.active, active, what + ": active");
		harness.expect(entry.file == configPath, what + ": file stamp");
	};

	expectEntry(0, 1, ConfigLoadTraceEntry::Kind::Eval, ConfigLoadTraceEntry::Result::NotEvaluated, false, "Eval");
	harness.expect(collector.entries[0].text == L"5", "Eval reports the computed value");
	harness.expectFalse(collector.entries[0].error, "Eval reports no error");
	expectEntry(1, 2, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::True, true, "outer If");
	expectEntry(2, 4, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::False, false, "nested If");
	expectEntry(3, 5, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false, "swallowed Delay");
	expectEntry(4, 7, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::NotEvaluated, false, "short-circuited ElseIf");
	expectEntry(5, 8, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false, "swallowed Preamp");
	expectEntry(6, 9, ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, false, "dead Else");
	expectEntry(7, 10, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false, "swallowed Preamp inside the dead Else");
	expectEntry(8, 12, ConfigLoadTraceEntry::Kind::InlineValue, ConfigLoadTraceEntry::Result::NotEvaluated, false, "inline value");
	// The engine trims only the command key, not the parameter text, so the
	// substituted string keeps the space after the colon - the entry reports
	// exactly what the downstream factories saw.
	harness.expect(collector.entries[8].text == L" -5 dB", "inline substitution reports the resolved parameters");

	// A second load without a sink must not crash and must add nothing.
	engine.setLoadTraceSink(nullptr);
	engine.loadConfig(configPath);
	harness.expectEqual((int)collector.entries.size(), (int)entries.size(),
		"detached sink receives nothing on a reload");
}

// The parse-error channel that replaced the engine's guess. What matters is the
// pair of judgements: a factory's own broken line is reported, and a line no
// factory claimed is not - because prose and notes are how 1.4.2 configurations
// carry comments, and reporting those would bury the real diagnostics.
void testParseErrorsAreReportedPerLineAndProseIsNot(test::Harness& harness)
{
	const std::wstring configPath = writeConfig(harness, L"parse-errors.txt",
		"Convolution:\n"                    // line 1: its own command, no path
		"Channel:\n"                        // line 2: its own command, no channel
		"Copy:\n"                           // line 3: its own command, no assignment
		"GraphicEQ:\n"                      // line 4: its own command, no nodes
		"remember to try 2 dB less here\n"  // line 5: prose, not an error
		"copy: a note to self\n"            // line 6: wrong case, so prose
		"Preamp: -3 dB\n");                 // line 7: fine, and must still run

	struct Collector : ConfigLoadTraceSink
	{
		std::vector<ConfigLoadTraceEntry> entries;
		void addEntry(const ConfigLoadTraceEntry& entry) override
		{
			entries.push_back(entry);
		}
	};
	Collector collector;

	FilterEngine engine;
	engine.setLoadTraceSink(&collector);
	initializeEngine(engine, 48000, 2, 512, configPath);

	std::vector<int> errorLines;
	for (const ConfigLoadTraceEntry& entry : collector.entries)
	{
		if (entry.kind != ConfigLoadTraceEntry::Kind::ParseError)
			continue;
		harness.expect(entry.error, "a parse error is flagged as one");
		harness.expect(!entry.text.empty(), "and carries a reason, which is the whole point of moving the diagnosis into the factory");
		harness.expect(entry.file == configPath, "stamped with the file it is in");
		errorLines.push_back(entry.line);
	}

	harness.requireEqual(errorLines.size(), size_t(4),
		"one report per broken line, and none for the two lines that are prose");
	harness.expect(errorLines[0] == 1 && errorLines[1] == 2 && errorLines[2] == 3 && errorLines[3] == 4,
		"the reports land on the lines that are broken, in order");

	// The load kept going: half a configuration is still worth running, and a
	// broken line must not take the working ones below it with it. loadConfig
	// answers false only when the whole load failed.
	harness.expect(engine.loadConfig(configPath),
		"a configuration with four unusable lines still loads, because the working lines below them have to run");
}

void testAnalysisFreezesDynamicVelvetAndLabelsTheSnapshot(
	test::Harness& harness)
{
	const std::wstring dynamicPath = writeConfig(harness, L"analysis-velvet-dynamic.txt",
		"Velvet: Mode=Dynamic Amount=100% Length=27.5625ms "
		"Density=1088.435/s Evolution=5s Transition=250ms "
		"Decay=-60dB Variation=2050083136\n");
	const std::wstring staticPath = writeConfig(harness, L"analysis-velvet-static.txt",
		"Velvet: Mode=Static Amount=100% Length=27.5625ms "
		"Density=1088.435/s Evolution=5s Transition=250ms "
		"Decay=-60dB Variation=2050083136\n");

	FilterEngine analysis;
	analysis.setAnalysisMode(true);
	initializeEngine(analysis, 48000, 2, 256, dynamicPath);
	harness.expect(analysis.usedFrozenDynamicAnalysis(),
		"analysis freezes Dynamic Velvet to one deterministic kernel and labels the response");
	harness.expect(analysis.loadConfig(staticPath),
		"analysis can reload a static Velvet configuration");
	harness.expect(!analysis.usedFrozenDynamicAnalysis(),
		"a static Velvet response is not labelled as a frozen dynamic snapshot");

	FilterEngine runtime;
	initializeEngine(runtime, 48000, 2, 256, dynamicPath);
	harness.expect(!runtime.usedFrozenDynamicAnalysis(),
		"the real-time engine keeps Dynamic Velvet live and never reports an analysis snapshot");
}

} // namespace

// Defined in SampleIoTests.cpp next to this file.
void runSampleIoTests(test::Harness& harness);
void runConfigurationFileReaderTests(test::Harness& harness);
void runDeviceApoInfoTests(test::Harness& harness);
void runRegistryTransactionTests(test::Harness& harness);
void runInstallDiagnosticsTests(test::Harness& harness);

int runEngineOrchestrationTests()
{
	LogHelper::set(stderr, false, false, false);

	test::Harness harness("EngineOrchestrationTests");

	try
	{
		harness.expect(ComBoundary::invoke([]() -> HRESULT {
			throw std::bad_alloc();
		}) == E_OUTOFMEMORY, "COM boundary maps allocation failure");
	}
	catch (...)
	{
		harness.fail("allocation exception escaped the COM boundary");
	}
	try
	{
		harness.expect(ComBoundary::invoke([]() -> HRESULT {
			throw std::runtime_error("injected COM failure");
		}) == E_UNEXPECTED, "COM boundary maps unexpected exception");
	}
	catch (...)
	{
		harness.fail("unexpected exception escaped the COM boundary");
	}
	harness.expect(ComBoundary::invoke([] { return S_FALSE; }) == S_FALSE,
		"COM boundary preserves callback HRESULT");

	testLogHelperFileDestination(harness);
	testLogHelperUserDestination(harness);
	testRegistryExportHeaderPreservesQualifiedRoot(harness);
	testSynchronizedStateSerializesReplacement(harness);
	testDeviceApoRegistryVocabulary(harness);
	testInstallStateComparisonIgnoresPadding(harness);
	runRegistryTransactionTests(harness);
	runDeviceApoInfoTests(harness);
	runInstallDiagnosticsTests(harness);
	testProcessWithoutConfigurationDoesNotCrash(harness);
	testInitialLoadUsesPublicationChannel(harness);
	testConfigSwapChannelPermitRoundTrip(harness);
	testEmptyConfigurationExpandsRenderChannels(harness);
	testParallelExecutor(harness);
	testConfigWatcherBackoffAndPathRefresh(harness);
	runConfigurationFileReaderTests(harness);
	runSampleIoTests(harness);
	testChannelSelectorRouting(harness);
	testCopySwapsChannels(harness);
	testMultiConvolutionIgnoresChannelSelection(harness);
	testConfigSwapCrossfades(harness);
	testFailedConfigLoadKeepsActiveConfiguration(harness);
	testRealBrirCrossfeed(harness);
	testConfigLoadTrace(harness);
	testParseErrorsAreReportedPerLineAndProseIsNot(harness);
	testAnalysisFreezesDynamicVelvetAndLabelsTheSnapshot(harness);

	removeTestDirectory();
	harness.report();
	return 0;
}

int main()
{
	try
	{
		return runEngineOrchestrationTests();
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "EngineOrchestrationTests: unhandled exception: %s\n", error.what());
	}
	catch (...)
	{
		std::fprintf(stderr, "EngineOrchestrationTests: unhandled non-standard exception\n");
	}
	return EXIT_FAILURE;
}
