/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Self-contained runtime test for the VST2 hosting path of the engine's VST
	host classes - VSTPluginLibrary (LoadLibrary + GetProcAddress(VSTPluginMain))
	and VSTPluginInstance (implemented by VST2Instance.cpp).
	It loads the companion TestVst2Plugin.dll (built
	from Tests/TestVst2Plugin from our own source, so it always matches the host
	architecture) and round-trips state plus audio through the engine's public
	host API.

	This is the runtime coverage complementing the config-line parsing tests
	(VSTPluginCommandTests) - actually loading a plugin, processing audio, and
	round-tripping chunk state - without depending on any plugin installed on
	the machine.

	A missing TestVst2Plugin.dll fails the suite: the HybridConvTests project
	copies the DLL next to HybridConvTests.exe as a post-build step (which is
	what makes the executable-relative lookup work on both x64 and ARM64), so
	its absence is a build or copy problem. The one path that still skips is
	a test executable whose own directory cannot be read; it prints a
	"skipped" line.

	VST headers: this translation unit includes VSTPluginLibrary.h and
	VSTPluginInstance.h, exactly as VSTPluginInstance.cpp does.
	VSTPluginLibrary.h pulls in the VST2 ABI (vst/aeffectx.h) and two base
	Steinberg headers (ipluginbase, smartpointer) for the factory it owns;
	VSTPluginInstance.h includes no SDK header since the instance was split by
	format. They resolve through $(VST3_SDK), already on this project's
	include path. No additional include directory is required.
*/

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include "vst/VSTPluginLibrary.h"
#include "vst/VSTChunkBase64.h"
#include "Tests/TestVst2Plugin/TestVst2Protocol.h"
#include "vst/PluginResidency.h"
#include "vst/VSTPluginInstance.h"
#include "filters/VSTPluginCommand.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "filters/loudnessCorrection/VolumeController.h"
#include "Tests/TestDirectory.h"
#include "Editor/helpers/VstPanelCall.h"
#include "services/logging/Logging.h"
#include "../TestHarness.h"
#include "platform/windows/WindowsPath.h"

using std::shared_ptr;
using std::unordered_map;
using std::vector;
using std::wstring;

namespace
{
test::HarnessAccess harness;

class RejectingLibrary : public AbstractLibrary
{
public:
	explicit RejectingLibrary(wstring path)
		: path(std::move(path))
	{
	}

	wstring getLibPath() override
	{
		return path;
	}

	int getCustomInitializeCount() const
	{
		return customInitializeCount;
	}

protected:
	bool loadFunctions() override
	{
		return true;
	}

	InitializeResult customInitialize() override
	{
		++customInitializeCount;
		return InitializeResult::FunctionsMissing;
	}

private:
	wstring path;
	int customInitializeCount = 0;
};

using testvst2::ChunkBlob;
using testvst2::kChunkMagic;
using testvst2::kChunkVersion;

// The plugin DLL is copied next to the running test executable by the
// HybridConvTests post-build step.
using pathutil::exeDirectory;
using pathutil::fileExists;

// Base64-encode a ChunkBlob the way the engine stores chunk state, so it can be
// fed straight into VSTPluginInstance::writeToEffect.
wstring encodeChunk(const ChunkBlob& blob)
{
	wstring encoded;
	vstchunk::encodeBase64(&blob, sizeof(blob), encoded);
	return encoded;
}

// Decode a base64 chunk string back into a ChunkBlob. Returns false if the
// payload is the wrong size or magic.
bool decodeChunk(const wstring& chunkData, ChunkBlob& out)
{
	if (chunkData.empty())
		return false;

	DWORD byteLength = 0;
	CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &byteLength, nullptr, nullptr);
	if (byteLength != sizeof(ChunkBlob))
		return false;

	if (CryptStringToBinaryW(chunkData.c_str(), 0, CRYPT_STRING_BASE64,
			reinterpret_cast<BYTE*>(&out), &byteLength, nullptr, nullptr) != TRUE)
		return false;
	return out.magic == kChunkMagic;
}

void expectRejectedMetadataPassesThrough(const shared_ptr<VSTPluginLibrary>& library,
	const wchar_t* mode, const std::string& label)
{
	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", mode);

	ChunkBlob gainBlob = {};
	gainBlob.magic = kChunkMagic;
	gainBlob.version = kChunkVersion;
	gainBlob.gain = 0.5f;

	{
		VSTPluginFilter filter(library, encodeChunk(gainBlob), unordered_map<wstring, float>());
		filter.initialize(48000.0f, 4, {L"L", L"R"});

		double inLeft[4] = {0.25, 0.5, 0.75, 1.0};
		double inRight[4] = {-0.25, -0.5, -0.75, -1.0};
		double outLeft[4] = {};
		double outRight[4] = {};
		double* input[2] = {inLeft, inRight};
		double* output[2] = {outLeft, outRight};
		filter.process(output, input, 4);

		bool unchanged = true;
		for (int i = 0; i < 4; ++i)
		{
			if (!test::nearlyEqual(outLeft[i], inLeft[i], 1.0e-9) || !test::nearlyEqual(outRight[i], inRight[i], 1.0e-9))
				unchanged = false;
		}
		harness.expectTrue(unchanged, label + ": malformed plugin is bypassed");
	}

	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
}

// Streams a unit impulse at sample 0 on both channels, then silence, through a
// fresh gain-0.5 filter over {L, R} in blocks of blockSize frames (max frame
// count 1024), and reports whether every output sample is 0.5 x the input
// delayed by expectedDelay samples.
bool latencyStreamMatches(const shared_ptr<VSTPluginLibrary>& library, unsigned blockSize, unsigned expectedDelay)
{
	constexpr unsigned maxFrameCount = 1024;
	constexpr unsigned streamLength = 2048;

	ChunkBlob gainBlob = {};
	gainBlob.magic = kChunkMagic;
	gainBlob.version = kChunkVersion;
	gainBlob.gain = 0.5f;

	VSTPluginFilter filter(library, encodeChunk(gainBlob), unordered_map<wstring, float>());
	filter.initialize(48000.0f, maxFrameCount, {L"L", L"R"});

	vector<double> inLeft(streamLength, 0.0);
	vector<double> inRight(streamLength, 0.0);
	inLeft[0] = 1.0;
	inRight[0] = 1.0;
	vector<double> outLeft(streamLength, -1.0);
	vector<double> outRight(streamLength, -1.0);
	for (unsigned offset = 0; offset < streamLength; offset += blockSize)
	{
		double* input[2] = {inLeft.data() + offset, inRight.data() + offset};
		double* output[2] = {outLeft.data() + offset, outRight.data() + offset};
		filter.process(output, input, blockSize);
	}

	for (unsigned i = 0; i < streamLength; ++i)
	{
		const double expected = i >= expectedDelay ? 0.5 * inLeft[i - expectedDelay] : 0.0;
		if (outLeft[i] != expected || outRight[i] != expected)
			return false;
	}
	return true;
}

shared_ptr<VSTPluginLibrary> loadScenarioLibrary()
{
	auto loaded = VSTPluginLibrary::load(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(loaded.succeeded(), "scenario VST2 module loads");
	return loaded.library;
}

// Audit #348 A10: the plug-in reports 512 samples of latency. The compensation
// delays only the channels no plug-in output writes (maintainer decision; it
// used to delay every channel, the processed ones included, which left them
// twice as late). A VST2 plug-in's instances cover every channel, so nothing
// is delayed here and no ring is allocated; Vst3HostTests covers a fill that
// leaves a channel unwritten.
TEST_CASE(VstHostTests, expectLatencyCompensationGolden)
{
	auto library = loadScenarioLibrary();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", L"latency-512");

	ChunkBlob gainBlob = {};
	gainBlob.magic = kChunkMagic;
	gainBlob.version = kChunkVersion;
	gainBlob.gain = 0.5f;
	VSTPluginFilter latencyFilter(library, encodeChunk(gainBlob), unordered_map<wstring, float>());
	latencyFilter.initialize(48000.0f, 1024, {L"L", L"R"});
	const vector<unsigned> latencyInput = {5, 9};
	vector<unsigned> latencyOutput = latencyInput;
	latencyFilter.reportLatency(latencyInput, latencyOutput);
	harness.expectTrue(latencyOutput == vector<unsigned>({521, 521}),
		"the VST2 filter reports its 512-sample latency on every written channel");

	harness.expectTrue(latencyStreamMatches(library, 128, 0),
		"latency-512: 128-frame blocks come out 0.5 x input with no extra delay");
	harness.expectTrue(latencyStreamMatches(library, 1024, 0),
		"latency-512: 1024-frame blocks come out 0.5 x input with no extra delay");

	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
}

TEST_CASE(VstHostTests, testForeignParameterNames)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "parameter name fixture loads");
	for (const wchar_t* mode : {L"untouched-name", L"full-name"})
	{
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", mode);
		auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
		const bool initialized = instance->initialize();
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
		harness.require(initialized, "parameter name fixture initializes");
		wstring chunk;
		unordered_map<wstring, float> parameters;
		instance->readFromEffect(chunk, parameters);
		const wstring gainName = wcscmp(mode, L"full-name") == 0
			? wstring(VST_BUFFER_SIZE_PARAM_NAME, L'N') : L"Gain";
		harness.expectTrue(parameters.size() == 2 && parameters.count(L"") == 1 && parameters.count(gainName) == 1,
			"an untouched parameter name is empty and a full name stops at its bound (audit #467 TD-52)");
		instance->writeToEffect(L"", {{gainName, 0.25f}, {L"", 1.0f}});
		instance->readFromEffect(chunk, parameters);
		harness.expectTrue(parameters.count(L"") == 1 && parameters[L""] == 1.0f,
			"state restoration zeroes each parameter-name buffer too");
	}
}

TEST_CASE(VstHostTests, testZeroStateResult)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "zero-return state fixture loads");
	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", L"state-zero-success");
	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
	const bool initialized = instance->initialize();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
	harness.require(initialized, "zero-return state fixture initializes");
	const wstring chunk = encodeChunk({kChunkMagic, kChunkVersion, 0.5f, 0.0f});
	harness.expectTrue(static_cast<bool>(instance->writeToEffect(chunk, {})),
		"a VST2 zero return is not a portable refusal signal");
	wstring restored;
	unordered_map<wstring, float> parameters;
	instance->readFromEffect(restored, parameters);
	harness.expectTrue(restored == chunk, "zero-return fixture actually restores its state");
	const auto truncated = instance->writeToEffect(L"V", {});
	harness.expectTrue(!truncated && truncated.reason != nullptr, "VST2 truncated state returns a reason");
}

TEST_CASE(VstHostTests, testStateRestoreFailureIsReported)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "restore failure fixture loads");
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "restore failure log opens");
	Logging::useStream(logFile, false, true, false);
	{
		// A valid saved chunk truncated to its first base64 character.
		VSTPluginFilter filter(library, L"V", {});
		filter.initialize(48000.0f, 4, {L"L", L"R", L"RL", L"RR"});
	}
	std::fflush(logFile);
	std::rewind(logFile);
	wstring log;
	wchar_t text[1024] = {};
	while (std::fgetws(text, 1024, logFile) != nullptr)
		log.append(text);
	Logging::useStream(stdout, true, true, false);
	std::fclose(logFile);
	const size_t first = log.find(L"could not restore its state");
	harness.expectTrue(first != wstring::npos && log.find(L"could not restore its state", first + 1) == wstring::npos,
		"a truncated VST2 chunk logs one failure even with split instances (audit #467 TD-36)");
	harness.expectTrue(log.find(library->getLibPath()) != wstring::npos, "state restore failure includes the library path");
}

TEST_CASE(VstHostTests, testEngineFaultContainment)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "engine fault fixture loads");
	for (const wchar_t* mode : {L"process", L"prepare", L"state"})
	{
		FILE* logFile = nullptr;
		harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "fault log capture opens");
		Logging::useStream(logFile, false, true, false);
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", mode);
		{
			VSTPluginFilter filter(library, encodeChunk({kChunkMagic, kChunkVersion, 0.5f, 0.0f}), {});
			filter.initialize(48000.0f, 4, {L"L", L"R"});
			SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
			double inLeft[] = {0.25, 0.5, 0.75, 1.0}, inRight[] = {-0.25, -0.5, -0.75, -1.0};
			double outLeft[4] = {}, outRight[4] = {};
			double* input[] = {inLeft, inRight};
			double* output[] = {outLeft, outRight};
			for (int block = 0; block < 2; block++)
			{
				filter.process(output, input, 4);
				for (int frame = 0; frame < 4; frame++)
				{
					harness.expectNear(outLeft[frame], inLeft[frame], 0.0, "faulted VST2 passes the current and later blocks through");
					harness.expectNear(outRight[frame], inRight[frame], 0.0, "faulted VST2 preserves both input channels");
				}
			}
			if (wcscmp(mode, L"process") == 0)
				harness.expectEqual(std::ftell(logFile), 0L, "audio-thread crash report is deferred until teardown");
		}
		std::fflush(logFile);
		std::rewind(logFile);
		wstring log;
		wchar_t text[1024] = {};
		while (std::fgetws(text, 1024, logFile) != nullptr)
			log.append(text);
		Logging::useStream(stdout, true, true, false);
		std::fclose(logFile);
		const wchar_t* marker = wcscmp(mode, L"process") == 0
			? L"crashed during audio processing" : L"crashed while preparing for processing";
		const size_t first = log.find(marker);
		harness.expectTrue(first != wstring::npos && log.find(marker, first + 1) == wstring::npos,
			"VST2 crash is logged once, with its library path");
		harness.expectTrue(log.find(library->getLibPath()) != wstring::npos, "VST2 crash log identifies the library");
	}
}

TEST_CASE(VstHostTests, testNonFiniteOutputContainment)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "non-finite VST2 fixture loads");
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "non-finite VST2 log capture opens");
	Logging::useStream(logFile, false, true, false);
	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", L"NanOutput");
	{
		VSTPluginFilter filter(library, encodeChunk({kChunkMagic, kChunkVersion, 0.5f, 0.0f}), {});
		filter.initialize(48000.0f, 4, {L"L", L"R"});
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
		double inLeft[] = {0.25, 0.5, 0.75, 1.0}, inRight[] = {-0.25, -0.5, -0.75, -1.0};
		double outLeft[4] = {}, outRight[4] = {};
		double* input[] = {inLeft, inRight};
		double* output[] = {outLeft, outRight};
		for (int block = 0; block < 2; block++)
		{
			filter.process(output, input, 4);
			for (int frame = 0; frame < 4; frame++)
			{
				harness.expectTrue(std::isfinite(outLeft[frame]) && std::isfinite(outRight[frame]),
					"non-finite VST2 output never escapes the filter");
				harness.expectNear(outLeft[frame], inLeft[frame], 0.0,
					"non-finite VST2 passes the current and later blocks through");
				harness.expectNear(outRight[frame], inRight[frame], 0.0,
					"non-finite VST2 preserves every channel");
			}
		}
		harness.expectEqual(std::ftell(logFile), 0L, "non-finite VST2 report is deferred until teardown");
	}
	std::fflush(logFile);
	std::rewind(logFile);
	wstring log;
	wchar_t text[1024] = {};
	while (std::fgetws(text, 1024, logFile) != nullptr)
		log.append(text);
	Logging::useStream(stdout, true, true, false);
	std::fclose(logFile);
	const wchar_t* marker = L"produced NaN or infinite samples";
	const size_t first = log.find(marker);
	harness.expectTrue(first != wstring::npos && log.find(marker, first + 1) == wstring::npos,
		"non-finite VST2 output is reported exactly once");
	harness.expectTrue(log.find(library->getLibPath()) != wstring::npos,
		"non-finite VST2 report identifies the library");
}

TEST_CASE(VstHostTests, testStateDependentLatencyIsRefreshed)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "state-latency VST2 fixture loads");
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "state-latency VST2 log capture opens");
	Logging::useStream(logFile, false, true, false);
	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", L"StateLatency");
	{
		VSTPluginFilter filter(library, encodeChunk({kChunkMagic, kChunkVersion, 0.5f, 0.0f}), {});
		filter.initialize(48000.0f, 4, {L"L", L"R"});
	}
	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
	std::fflush(logFile);
	std::rewind(logFile);
	wstring log;
	wchar_t text[1024] = {};
	while (std::fgetws(text, 1024, logFile) != nullptr)
		log.append(text);
	Logging::useStream(stdout, true, true, false);
	std::fclose(logFile);
	harness.expectTrue(log.find(L"reports 64 samples of latency after loading its state (0 before)") != wstring::npos,
		"VST2 load reports the post-state latency used for compensation");
}

bool panelFaultEscapes(VSTPluginInstance* instance, bool restore,
	const wstring& chunk, const unordered_map<wstring, float>& parameters, bool& escaped)
{
	__try
	{
		return vstPanelCallGuarded([&]() {
			if (restore)
			{
				instance->writeToEffect(chunk, parameters);
				return true;
			}
			short width = 0, height = 0;
			return instance->startEditing(nullptr, &width, &height);
		});
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		escaped = true;
		return false;
	}
}

TEST_CASE(VstHostTests, testGuardedPanelFaults)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "panel fault fixture loads");
	for (const wchar_t* mode : {L"state", L"editor"})
	{
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", mode);
		auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
		const bool initialized = instance->initialize();
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
		harness.require(initialized, "panel fault fixture initializes");
		bool escaped = false;
		const bool opened = panelFaultEscapes(instance.get(), wcscmp(mode, L"state") == 0,
			encodeChunk({kChunkMagic, kChunkVersion, 0.5f, 0.0f}), {}, escaped);
		harness.expectFalse(escaped, "panel state/editor AV is contained by the shared guard (audit #467 TD-20)");
		harness.expectFalse(opened, "a faulted panel operation reports failure");
	}
}

TEST_CASE(VstHostTests, testRejectedNegotiationMetadata)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "metadata fixture loads");
	for (const wchar_t* mode : {L"huge-inputs", L"huge-outputs", L"negative-inputs", L"huge-delay"})
	{
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", mode);
		auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
		const bool initialized = instance->initialize();
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
		harness.require(initialized, "malformed metadata fixture initializes");
		for (int width : {0, 2})
		{
			VSTPluginInstance::NegotiationRequest request;
			request.channelCount = width;
			const auto result = instance->negotiate(request);
			harness.expectTrue(!result.accepted && !result.reason.empty(),
				"engine and preview refuse absurd metadata with a reason");
		}
	}
}

TEST_CASE(VstHostTests, testProcessBlockWidthsAndLease)
{
	auto library = VSTPluginLibrary::getInstance(exeDirectory() + L"\\TestVst2Plugin.dll");
	harness.require(VSTPluginLibrary::load(library->getLibPath()).succeeded(), "width fixture loads");
	for (const wchar_t* mode : {L"double", L"float", L"accumulate"})
	{
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", mode);
		auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
		const bool initialized = instance->initialize();
		SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
		harness.require(initialized, "width fixture initializes");
		harness.require(instance->negotiate({}).accepted, "width fixture negotiates");
		harness.require(instance->prepareForProcessing(48000.0f, 4, VSTPluginInstance::InputWidth::Float),
			"width fixture prepares conversion buffers");
		instance->writeToEffect(encodeChunk({kChunkMagic, kChunkVersion, 0.5f, 0.0f}), {});
		{
			VSTPluginInstance::ProcessingLease owner;
			harness.require(owner.acquire(instance.get()), "lease starts idle processing");
			{
				VSTPluginInstance::ProcessingLease borrower;
				harness.require(borrower.acquire(instance.get()), "lease borrows active processing");
			}
			harness.expectTrue(instance->canProcessNow(), "borrower does not stop the owner");
			double left[] = {0.25, 0.5, 0.75, 1.0}, right[] = {-0.25, -0.5, -0.75, -1.0};
			double outLeft[4] = {}, outRight[4] = {};
			double* input[] = {left, right};
			double* output[] = {outLeft, outRight};
			for (int block = 0; block < 2; block++)
			{
				std::fill_n(outLeft, 4, 9.0);
				std::fill_n(outRight, 4, 9.0);
				instance->processBlock(input, output, 4);
				for (int frame = 0; frame < 4; frame++)
				{
					harness.expectNear(outLeft[frame], left[frame] * 0.5, 1e-9, "selected width matches engine left output");
					harness.expectNear(outRight[frame], right[frame] * 0.5, 1e-9, "selected width matches engine right output");
				}
			}
			float previewIn[] = {0.25f, 0.5f, 0.75f, 1.0f}, previewOut[4] = {}, previewRight[4] = {};
			float* previewInputs[] = {previewIn, previewIn};
			float* previewOutputs[] = {previewOut, previewRight};
			instance->processBlock(previewInputs, previewOutputs, 4);
			harness.expectNear(previewOut[3], 0.5, 1e-9, "float preview uses the selected plug-in width");
		}
		harness.expectFalse(instance->canProcessNow(), "owner stops processing when its scope ends");
	}
}

TEST_CASE(VstHostTests, testVolumeControllerBalancesComInitialization)
{
	bool threadStartedUninitialized = false;
	bool threadEndedUninitialized = false;
	std::thread worker([&]()
	{
		ULONG_PTR token = 0;
		threadStartedUninitialized = CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
		{
			VolumeController controller;
		}
		threadEndedUninitialized = CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
	});
	worker.join();

	harness.expectTrue(threadStartedUninitialized, "COM balance test starts on an uninitialized thread");
	harness.expectTrue(threadEndedUninitialized, "VolumeController balances COM initialization");
}
// The DLL is held against writers from the judgment to LoadLibraryW, the
// window in which an emptied leaf could otherwise be given reparse data
// (AbstractLibrary::holdForLoad). The hold must not stop the loader itself.
TEST_CASE(VstHostTests, testLoadHold)
{
	const wstring dllPath = exeDirectory() + L"\\TestVst2Plugin.dll";

	test::TestDirectory directory(L"VstHostTests-load");
	const wstring folder = directory.subdirectory(L"load-hold");
	const wstring plugin = folder + L"\\plugin.dll";
	const wstring empty = folder + L"\\empty.dll";
	harness.require(CopyFileW(dllPath.c_str(), plugin.c_str(), FALSE) != FALSE, "copy the test plugin for the hold test");
	{
		const auto judged = ConfigFileReference::library(L"", plugin, L"");
		harness.require(judged.refusal.empty() && judged.path.leaf() != nullptr, "hold test plugin is judged");
		winutil::UniqueHandle held;
		harness.expectEqual(AbstractLibrary::holdForLoad(judged.path.leaf(), held), DWORD(ERROR_SUCCESS),
			"a local plugin with data is held for loading");
		harness.expectTrue(static_cast<bool>(held), "the hold is a handle of its own");
		const winutil::UniqueHandle writer(CreateFileW(plugin.c_str(), FILE_WRITE_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		const DWORD writerError = GetLastError();
		harness.expectTrue(!writer && writerError == ERROR_SHARING_VIOLATION,
			"while held, nobody can open the plugin to empty it");
		HMODULE module = LoadLibraryW(plugin.c_str());
		harness.expectTrue(module != nullptr, "the loader opens a held plugin as before");
		if (module != nullptr)
			FreeLibrary(module);
	}
	{
		const winutil::UniqueHandle writer(CreateFileW(plugin.c_str(), FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		harness.require(static_cast<bool>(writer), "open a writer before the hold");
		const auto judged = ConfigFileReference::library(L"", plugin, L"");
		harness.require(judged.path.leaf() != nullptr, "a plugin open for writing is still judged");
		winutil::UniqueHandle held;
		harness.expectEqual(AbstractLibrary::holdForLoad(judged.path.leaf(), held), DWORD(ERROR_SHARING_VIOLATION),
			"a plugin another program is writing is not held");
		shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(plugin);
		harness.expectTrue(VSTPluginLibrary::load(library->getLibPath(), &judged.path).status == AbstractLibrary::InitializeResult::LoadingFailed,
			"and not loaded, as LoadLibraryW would have refused it too");
	}
	{
		const winutil::UniqueHandle create(CreateFileW(empty.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr));
		harness.require(static_cast<bool>(create), "create an empty plugin file");
	}
	{
		const auto judged = ConfigFileReference::library(L"", empty, L"");
		harness.require(judged.path.leaf() != nullptr, "an empty file is judged");
		winutil::UniqueHandle held;
		harness.expectEqual(AbstractLibrary::holdForLoad(judged.path.leaf(), held), DWORD(ERROR_BAD_EXE_FORMAT),
			"an empty file, the one file that can become a link, is refused");
		shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(empty);
		harness.expectTrue(VSTPluginLibrary::load(library->getLibPath(), &judged.path).status == AbstractLibrary::InitializeResult::LoadingFailed,
			"an empty file fails to load as it did before");
	}
	DeleteFileW(empty.c_str());
	DeleteFileW(plugin.c_str());
	RemoveDirectoryW(folder.c_str());
	directory.removeAll();
}

void copyResidencyTestPlugin(const wstring& source, const wstring& destination, const std::string& label)
{
	if (CopyFileW(source.c_str(), destination.c_str(), FALSE) == FALSE)
		harness.fail(label + " (error " + std::to_string(GetLastError()) + ")");
}

TEST_CASE(VstHostTests, testExpiredLibraryCache)
{
	const wstring dir = exeDirectory();

	const size_t baseline = VSTPluginLibrary::cachedLibraryCountForTesting();
	for (int i = 0; i < 16; i++)
	{
		const wstring path = dir + L"\\missing-cache-plugin-" + std::to_wstring(i) + L".dll";
		{
			auto library = VSTPluginLibrary::getInstance(path);
			harness.expectEqual(VSTPluginLibrary::cachedLibraryCountForTesting(), baseline + 1,
				"a live metadata library occupies one cache entry");
			auto second = VSTPluginLibrary::getInstance(path);
			harness.expectEqual(second.use_count(), 2L,
				"metadata lookups share the live library");
		}
		harness.expectEqual(VSTPluginLibrary::cachedLibraryCountForTesting(), baseline,
			"destroying the last library reference erases its expired key");
	}
	PluginResidency::keepLoadedForProcessLifetime(true);
	{
		auto failed = VSTPluginLibrary::load(dir + L"\\missing-cache-plugin.dll");
		harness.expectTrue(!failed.succeeded(), "a missing library fails through load");
	}
	PluginResidency::keepLoadedForProcessLifetime(false);
	harness.expectEqual(VSTPluginLibrary::cachedLibraryCountForTesting(), baseline,
		"failed loads are not retained by the process-lifetime policy");
}

TEST_CASE(VstHostTests, testVst2CallbacksAndEditorDefaults)
{
	auto library = loadScenarioLibrary();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_HOST_CALLBACKS", L"1");
	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	const bool initialized = instance->initialize();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_HOST_CALLBACKS", nullptr);
	harness.require(initialized, "null host product/canDo pointers are refused and valid requests succeed");
	short width = 0;
	short height = 0;
	harness.expectFalse(instance->startEditing(nullptr, &width, &height, 2.0),
		"the editorless fixture refuses editing");
	harness.expectEqual(width, short(200), "failed editor open keeps the scaled default width");
	harness.expectEqual(height, short(150), "failed editor open keeps the scaled default height");
	instance->startEditing(nullptr, &width, &height, 0.0);
	harness.expectEqual(width, short(400), "an invalid editor scale is normalized");
	harness.expectEqual(height, short(300), "normalized scale preserves the default height");
	int callbacks = 0;
	instance->setAutomateFunc([&callbacks]() { callbacks++; });
	instance->setSizeWindowFunc([&callbacks](int, int) { callbacks++; });
	instance.reset();
	harness.expectEqual(callbacks, 0, "VST2 teardown cannot invoke caller closures");
}

TEST_CASE(VstHostTests, testVst2EditorScaling)
{
	auto library = loadScenarioLibrary();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", L"scaled");
	auto scaled = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	const bool scaledInitialized = scaled->initialize();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", nullptr);
	harness.require(scaledInitialized, "scaled VST2 editor fixture initializes");
	short width = 0;
	short height = 0;
	harness.expectTrue(scaled->startEditing(nullptr, &width, &height, 1.5),
		"scaled VST2 editor opens at 150 percent");
	harness.expectTrue(width == 320 && height == 200,
		"scaled VST2 editor reports logical dimensions after its pre-layout scale request");
	int resizedWidth = 0;
	int resizedHeight = 0;
	scaled->setSizeWindowFunc([&resizedWidth, &resizedHeight](int w, int h) {
		resizedWidth = w;
		resizedHeight = h;
	});
	harness.expectTrue(scaled->setEditorScale(2.0), "open scaled VST2 editor accepts a monitor scale change");
	harness.expectTrue(resizedWidth == 320 && resizedHeight == 200,
		"scaled VST2 resize callback reports logical dimensions");
	scaled->stopEditing();
	harness.expectFalse(scaled->setEditorScale(1.25), "closed VST2 editor refuses a monitor scale change");
	scaled->setSizeWindowFunc(nullptr);
	scaled.reset();

	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", L"fixed");
	auto fixed = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	const bool fixedInitialized = fixed->initialize();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", nullptr);
	harness.require(fixedInitialized, "fixed VST2 editor fixture initializes");
	width = 0;
	height = 0;
	harness.expectTrue(fixed->startEditing(nullptr, &width, &height, 1.5),
		"fixed VST2 editor opens at 150 percent");
	harness.expectTrue(width == 213 && height == 133,
		"fixed VST2 editor physical dimensions are converted to logical dimensions");
	resizedWidth = 0;
	resizedHeight = 0;
	fixed->setSizeWindowFunc([&resizedWidth, &resizedHeight](int w, int h) {
		resizedWidth = w;
		resizedHeight = h;
	});
	harness.expectTrue(fixed->setEditorScale(2.0), "open fixed VST2 editor accepts a monitor scale change");
	harness.expectTrue(resizedWidth == 160 && resizedHeight == 100,
		"fixed VST2 editor is re-reported in the new logical dimensions");
	fixed->stopEditing();
	fixed->setSizeWindowFunc(nullptr);
	fixed.reset();

	// The fixture keeps its last scale across editor sessions, so a session at
	// 100 percent after one at 150 percent shows whether 1.0 is sent too.
	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", L"scaled");
	auto reopened = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	const bool reopenedInitialized = reopened->initialize();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", nullptr);
	harness.require(reopenedInitialized, "reopened scaled VST2 editor fixture initializes");
	harness.expectTrue(reopened->startEditing(nullptr, &width, &height, 1.5),
		"scaled VST2 editor opens at 150 percent before reopening");
	reopened->stopEditing();
	width = 0;
	height = 0;
	harness.expectTrue(reopened->startEditing(nullptr, &width, &height, 1.0),
		"scaled VST2 editor reopens at 100 percent");
	harness.expectTrue(width == 320 && height == 200,
		"a reopened VST2 editor receives the 100 percent content scale request");
	reopened->stopEditing();

	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", L"late");
	auto late = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	const bool lateInitialized = late->initialize();
	SetEnvironmentVariableW(L"EAPO_TEST_VST_EDITOR", nullptr);
	harness.require(lateInitialized, "late-scaling VST2 editor fixture initializes");
	width = 0;
	height = 0;
	harness.expectTrue(late->startEditing(nullptr, &width, &height, 1.5),
		"late-scaling VST2 editor opens at 150 percent");
	harness.expectTrue(width == 320 && height == 200,
		"a VST2 editor that drops the pre-open scale receives it again after opening");
	late->stopEditing();
	late.reset();
}

TEST_CASE(VstHostTests, testProcessLifetimeResidency)
{
	const wstring dir = exeDirectory();
	const wstring dllPath = dir + L"\\TestVst2Plugin.dll";

	const wstring unloadPath = dir + L"\\TestVst2Plugin.Unload.dll";
	const wstring residentPath = dir + L"\\TestVst2Plugin.Resident.dll";
	copyResidencyTestPlugin(dllPath, unloadPath, "copy the unload-policy test plugin");
	copyResidencyTestPlugin(dllPath, residentPath, "copy the process-resident test plugin");

	{
		VSTPluginFilterFactory factory;
		wstring command = L"VSTPlugin";
		wstring parameters = L"Library \"" + unloadPath + L"\"";
		FilterVector filters = factory.createFilter(L"test-vst-unload.txt", command, parameters).filters;
		harness.requireEqual(filters.size(), (size_t)1,
			"the factory creates a filter while process-lifetime retention is off");
		harness.require(GetModuleHandleW(unloadPath.c_str()) != nullptr,
			"the unload-policy test module is loaded while its filter exists");
	}
	const bool unloaded = GetModuleHandleW(unloadPath.c_str()) == nullptr;
	harness.expectTrue(unloaded, "the default policy unloads the module after the last filter reference");
	if (unloaded)
		std::printf("VST residency policy off: module unloaded after filter destruction\n");

	PluginResidency::keepLoadedForProcessLifetime(true);
	HMODULE loadedModule = nullptr;
	{
		VSTPluginFilterFactory factory;
		wstring command = L"VSTPlugin";
		wstring parameters = L"Library \"" + residentPath + L"\"";
		FilterVector filters = factory.createFilter(L"test-vst-resident.txt", command, parameters).filters;
		harness.requireEqual(filters.size(), (size_t)1,
			"the factory creates a filter while process-lifetime retention is on");
		loadedModule = GetModuleHandleW(residentPath.c_str());
		harness.require(loadedModule != nullptr,
			"the resident test module is loaded while its filter exists");
	}

	const wstring workerPath = dir + L"\\TestVst2Plugin.WorkerResident.dll";
	copyResidencyTestPlugin(dllPath, workerPath, "copy the worker-resident test plugin");
	auto workerResult = AbstractLibrary::InitializeResult::LoadingFailed;
	std::thread loader([&]() {
		workerResult = VSTPluginLibrary::load(workerPath).status;
	});
	loader.join();
	harness.expectTrue(AbstractLibrary::succeeded(workerResult) && GetModuleHandleW(workerPath.c_str()) != nullptr,
		"a worker load retains the module without any completion callback");

	HMODULE retainedModule = GetModuleHandleW(residentPath.c_str());
	const bool retainedAfterFilter = retainedModule != nullptr && retainedModule == loadedModule;
	harness.expectTrue(retainedAfterFilter,
		"the resident module remains loaded with the same handle after filter destruction");
	harness.expectTrue(PluginResidency::holdsLoadedModules(),
		"the process-lifetime registry reports its retained module");

	const auto judgedResident = ConfigFileReference::library(L"", residentPath, L"");
	harness.require(judgedResident.refusal.empty() && judgedResident.path.leaf() != nullptr,
		"the retained plugin path is judged for the repeat initialization");
	shared_ptr<VSTPluginLibrary> retainedLibrary = VSTPluginLibrary::getInstance(residentPath);
	const auto repeatResult = VSTPluginLibrary::load(retainedLibrary->getLibPath(), &judgedResident.path).status;
	harness.expectTrue(repeatResult == AbstractLibrary::InitializeResult::AlreadyLoaded, "the retained library reports that it is already loaded");
	const bool reusedHandle = GetModuleHandleW(residentPath.c_str()) == loadedModule;
	harness.expectTrue(reusedHandle, "repeat initialization reuses the retained module handle");
	retainedLibrary.reset();
	PluginResidency::keepLoadedForProcessLifetime(false);

	if (retainedAfterFilter && repeatResult == AbstractLibrary::InitializeResult::AlreadyLoaded && reusedHandle && PluginResidency::holdsLoadedModules())
		std::printf("VST residency policy on: module retained and repeat initialization reused its handle\n");

	harness.expectTrue(DeleteFileW(unloadPath.c_str()) != FALSE,
		"the unloaded test plugin copy is deleted at the end of the test");
}
} // namespace

TEST_CASE(VstHostTests, testCompanionAvailable)
{
	const wstring dir = exeDirectory();
	if (dir.empty())
	{
		std::printf("VstHostTests skipped: could not resolve test executable directory\n");
return;
	}

	const wstring dllPath = dir + L"\\TestVst2Plugin.dll";
	if (!fileExists(dllPath))
	{
		// Audit #250 F049: this used to be a soft skip, letting a broken
		// TestVst2Plugin build (or a broken post-build copy) turn the whole
		// VST2 host suite green without running it. The VST3 side already
		// states the policy: a module we build ourselves being absent is a
		// build problem, and a build problem fails.
		harness.expectTrue(false,
			"TestVst2Plugin.dll is present next to the test executable "
			"(missing = build/copy problem, the suite cannot run)");
return;
	}
}

TEST_CASE(VstHostTests, testInitializationRollback)
{
	const wstring dir = exeDirectory();
	const wstring dllPath = dir + L"\\TestVst2Plugin.dll";
	// A failed subclass initialization must roll the DLL load back completely.
	// Otherwise the second call sees a non-null module and returns 0 (already
	// initialized), turning the original failure into a false success.
	RejectingLibrary rejectingLibrary(dllPath);
	harness.expectTrue(rejectingLibrary.initialize() == AbstractLibrary::InitializeResult::FunctionsMissing,
		"custom initialization failure is reported");
	harness.expectTrue(rejectingLibrary.initialize() == AbstractLibrary::InitializeResult::FunctionsMissing,
		"custom initialization failure is reported again after rollback");
	harness.expectEqual(rejectingLibrary.getCustomInitializeCount(), 2,
		"custom initialization is retried after rollback");
}

TEST_CASE(VstHostTests, testModuleDiscoveryAndAbi)
{
	const wstring dir = exeDirectory();
	const wstring dllPath = dir + L"\\TestVst2Plugin.dll";
	// Load the library through the engine's loader (LoadLibrary +
	// GetProcAddress(VSTPluginMain)). The typed result distinguishes a new
	// load, an already-loaded module and the reason for failure.
	const VSTPluginCommand importedCommand = VSTPluginCommand::parse(
		L"Library \"" + dllPath + L"\"");
	harness.expectTrue(importedCommand.libraryPath == dllPath,
		"imported config retains the external VST library path");
	shared_ptr<VSTPluginLibrary> library =
		VSTPluginLibrary::getInstance(importedCommand.libraryPath);
	harness.require(library != nullptr, "getInstance returned a library");

	const auto judgedLibrary = ConfigFileReference::library(dir, L"TestVst2Plugin.dll", L"");
	harness.require(judgedLibrary.refusal.empty() && judgedLibrary.path.leaf() != nullptr,
		"VST2 library reference retains a readable pinned leaf");
	auto loadResult = VSTPluginLibrary::load(library->getLibPath(), &judgedLibrary.path).status;
	if (!AbstractLibrary::succeeded(loadResult))
		std::printf("Dual-export .dll load status: %d\n", static_cast<int>(loadResult));
	harness.require(AbstractLibrary::succeeded(loadResult),
		"a .dll exporting both entry points loads as VST2 (#455)");
	harness.expectFalse(library->isVST3(),
		"a .dll exporting both entry points is marked as VST2 (#455)");
	harness.expectTrue(library->VSTPluginMain != nullptr,
		"a .dll exporting both entry points retains VSTPluginMain (#455)");
	harness.expectTrue(library->getFactory() == nullptr,
		"a .dll exporting both entry points does not retain a VST3 factory (#455)");
}

TEST_CASE(VstHostTests, testVst2IgnoresBusContract)
{
	const wstring dir = exeDirectory();
	const wstring dllPath = dir + L"\\TestVst2Plugin.dll";
	{
		VSTPluginFilterFactory factory;
		wstring busCommand = L"VSTPlugin";
		wstring busParameters = L"Library \"" + dllPath
			+ L"\" Input Stereo Output 7.1";
		FilterVector accepted = factory.createFilter(L"test-vst2-layout-ignore.txt", busCommand, busParameters).filters;
		harness.expectEqual(accepted.size(), (size_t)1,
			"VSTPlugin accepts Input/Output syntax for a loaded VST2 module");
		if (!accepted.empty())
		{
			VSTPluginFilter* filter = static_cast<VSTPluginFilter*>(accepted[0].get());
			harness.expectFalse(filter->getBusContract().has_value(),
				"VST2 quietly discards the VST3-only Input/Output contract");
			harness.expectFalse(filter->getStereoInput(),
				"ignored Input/Output does not enable the legacy StereoInput path");
		}
		harness.expectTrue(busCommand == L"VSTPlugin",
			"VST2 Input/Output handling keeps the shared VSTPlugin command");
	}
}

TEST_CASE(VstHostTests, testDualExportExtensionSelection)
{
	const wstring dir = exeDirectory();
	const wstring dllPath = dir + L"\\TestVst2Plugin.dll";
	const wstring dualExportVst3Path = dir + L"\\TestVst2DualExport.vst3";
	if (CopyFileW(dllPath.c_str(), dualExportVst3Path.c_str(), FALSE) != FALSE)
	{
		shared_ptr<VSTPluginLibrary> dualExportLibrary = VSTPluginLibrary::getInstance(dualExportVst3Path);
		harness.expectTrue(!VSTPluginLibrary::load(dualExportLibrary->getLibPath()).succeeded()
			&& dualExportLibrary->isVST3(),
			"a dual-export .vst3 uses its VST3 factory even when that factory has no classes");
	}
	else
		harness.expectTrue(false, "dual-export .vst3 copy is available for extension selection");
}

TEST_CASE(VstHostTests, testRejectedMetadataPassthrough)
{
	auto library = loadScenarioLibrary();
	expectRejectedMetadataPassesThrough(library, L"huge-inputs", "unrealistic input count");
	expectRejectedMetadataPassesThrough(library, L"negative-inputs", "negative input count");
	expectRejectedMetadataPassesThrough(library, L"huge-outputs", "unrealistic output count");
	expectRejectedMetadataPassesThrough(library, L"negative-outputs", "negative output count");
	expectRejectedMetadataPassesThrough(library, L"negative-delay", "negative initial delay");
	expectRejectedMetadataPassesThrough(library, L"huge-delay", "unrealistic initial delay");
}

TEST_CASE(VstHostTests, testInstanceMetadataAndLifecycle)
{
	auto library = loadScenarioLibrary();
	// Construct and initialize the instance the way the engine does (heap
	// allocated, owned here). processLevel mirrors a realtime audio thread.
	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	bool initialized = instance->initialize();
	harness.expectTrue(initialized, "VSTPluginInstance initialize succeeded");

	harness.expectEqual(instance->numInputs(), 2, "plugin reports 2 inputs");
	harness.expectEqual(instance->numOutputs(), 2, "plugin reports 2 outputs");
	harness.expectTrue(instance->prepareForProcessing(48000.0f, 512, VSTPluginInstance::InputWidth::Float), "plugin prepares float input processing");
	harness.expectTrue(instance->prepareForProcessing(48000.0f, 512), "plugin prepares double input processing");
	harness.expectTrue(instance->getName() == L"TestVst2Plugin", "plugin reports its name");

	// Audit #348 TD-14: this plugin has no editor (VST_EFFECT_FLAG_EDITOR is
	// clear). Opening its panel used to dereference an uninitialised rect.
	short editorWidth = 0;
	short editorHeight = 0;
	harness.expectFalse(instance->startEditing(nullptr, &editorWidth, &editorHeight),
		"startEditing on a VST2 plugin without an editor reports failure");

	instance->prepareForProcessing(48000.0f, 512);
	harness.expectFalse(instance->canProcessNow(),
		"a prepared VST2 instance cannot process before startProcessing");
	instance->startProcessing();
	harness.expectTrue(instance->canProcessNow(),
		"a started VST2 instance reports that it can process");

	instance->stopProcessing();
}

TEST_CASE(VstHostTests, testChunkRoundTrip)
{
	auto library = loadScenarioLibrary();
	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	harness.require(instance->initialize(), "scenario VST2 instance initializes");
	instance->prepareForProcessing(48000.0f, 512);
	instance->startProcessing();
	// --- Chunk round-trip: read default state, then set a known gain and read
	// it back. The plugin sets the programChunks flag, so the engine routes all
	// state through the chunk path (writeToEffect/readFromEffect).
	wstring chunkA;
	unordered_map<wstring, float> paramsA;
	instance->readFromEffect(chunkA, paramsA);
	harness.expectFalse(chunkA.empty(), "readFromEffect returned a non-empty chunk");

	ChunkBlob defaultBlob = {};
	harness.expectTrue(decodeChunk(chunkA, defaultBlob), "default chunk decodes with the expected magic");
	harness.expectEqual(defaultBlob.version, kChunkVersion, "default chunk version");
	harness.expectNear(defaultBlob.gain, 1.0, 1.0e-9, "default gain is unity");

	// Write a chunk that sets gain = 0.5, bypass off, and read it back.
	const float testGain = 0.5f;
	ChunkBlob writeBlob = {};
	writeBlob.magic = kChunkMagic;
	writeBlob.version = kChunkVersion;
	writeBlob.gain = testGain;
	writeBlob.bypass = 0.0f;
	const wstring writeChunk = encodeChunk(writeBlob);
	harness.expectFalse(writeChunk.empty(), "test chunk encoded to base64");

	instance->writeToEffect(writeChunk, unordered_map<wstring, float>());

	wstring chunkB;
	unordered_map<wstring, float> paramsB;
	instance->readFromEffect(chunkB, paramsB);
	ChunkBlob readBlob = {};
	harness.expectTrue(decodeChunk(chunkB, readBlob), "round-tripped chunk decodes");
	harness.expectNear(readBlob.gain, testGain, 1.0e-9, "gain survived the chunk write/read round-trip");
	harness.expectTrue(chunkB == writeChunk, "chunk string is stable after writing the same state");

	// Re-reading without an intervening write must return the identical string.
	wstring chunkC;
	unordered_map<wstring, float> paramsC;
	instance->readFromEffect(chunkC, paramsC);
	harness.expectTrue(chunkC == chunkB, "consecutive readFromEffect calls are stable");

	instance->stopProcessing();
}

TEST_CASE(VstHostTests, testProcessingFollowsState)
{
	auto library = loadScenarioLibrary();
	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	harness.require(instance->initialize(), "scenario VST2 instance initializes");
	instance->prepareForProcessing(48000.0f, 512);
	instance->startProcessing();
	const float testGain = 0.5f;
	instance->writeToEffect(encodeChunk({kChunkMagic, kChunkVersion, testGain, 0.0f}), {});
	// --- Audio: with gain = 0.5, processDoubleReplacing must produce out == in * 0.5.
	const int frameCount = 256;
	vector<double> inLeft(frameCount), inRight(frameCount);
	vector<double> outLeft(frameCount, 0.0), outRight(frameCount, 0.0);
	for (int i = 0; i < frameCount; ++i)
	{
		inLeft[i] = 0.25 + 0.001 * i;   // arbitrary but deterministic
		inRight[i] = -0.5 + 0.002 * i;
	}

	double* inArray[2] = { inLeft.data(), inRight.data() };
	double* outArray[2] = { outLeft.data(), outRight.data() };
	instance->processBlock(inArray, outArray, frameCount);

	bool audioMatches = true;
	for (int i = 0; i < frameCount && audioMatches; ++i)
	{
		if (!test::nearlyEqual(outLeft[i], inLeft[i] * testGain, 1.0e-9) || !test::nearlyEqual(outRight[i], inRight[i] * testGain, 1.0e-9))
			audioMatches = false;
	}
	harness.expectTrue(audioMatches, "processDoubleReplacing output equals input * gain");

	// Switch gain back to unity through another chunk write and confirm the
	// audio follows the new state (in == out within epsilon).
	ChunkBlob unityBlob = {};
	unityBlob.magic = kChunkMagic;
	unityBlob.version = kChunkVersion;
	unityBlob.gain = 1.0f;
	unityBlob.bypass = 0.0f;
	instance->writeToEffect(encodeChunk(unityBlob), unordered_map<wstring, float>());

	vector<double> unityOutLeft(frameCount, 0.0), unityOutRight(frameCount, 0.0);
	double* unityOutArray[2] = { unityOutLeft.data(), unityOutRight.data() };
	instance->processBlock(inArray, unityOutArray, frameCount);

	bool unityMatches = true;
	for (int i = 0; i < frameCount && unityMatches; ++i)
	{
		if (!test::nearlyEqual(unityOutLeft[i], inLeft[i], 1.0e-9) || !test::nearlyEqual(unityOutRight[i], inRight[i], 1.0e-9))
			unityMatches = false;
	}
	harness.expectTrue(unityMatches, "unity gain passes audio through unchanged");

	instance->stopProcessing();
	harness.expectFalse(instance->canProcessNow(),
		"a stopped VST2 instance no longer reports that it can process");

	// The owning pointer mirrors the engine and sends effClose on every exit,
	// including an unexpected exception from a later assertion.
}
