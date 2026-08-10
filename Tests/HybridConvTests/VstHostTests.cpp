/*
	This file is part of EqualizerAPO-XT.

	Self-contained runtime test for the VST2 hosting path of the engine's VST
	host classes - VSTPluginLibrary (LoadLibrary + GetProcAddress(VSTPluginMain))
	and VSTPluginInstance (VSTPluginInstance.cpp + .VST2.cpp + .State.cpp).
	It loads the companion TestVst2Plugin.dll (built
	from Tests/TestVst2Plugin from our own source, so it always matches the host
	architecture) and round-trips state plus audio through the engine's public
	host API.

	This is the runtime coverage complementing the config-line parsing tests
	(VSTPluginCommandTests) - actually loading a plugin, processing audio, and
	round-tripping chunk state - without depending on any plugin installed on
	the machine.

	Soft skip: if TestVst2Plugin.dll is not found next to the running test
	executable (e.g. the plugin project was not built or copied), the test
	prints a clear "skipped" line and returns without failing, so the suite
	never breaks the build. The HybridConvTests project copies the DLL next to
	HybridConvTests.exe as a post-build step, which is what makes the
	GetModuleFileName-relative lookup succeed on both x64 and ARM64.

	VST headers: this translation unit includes VSTPluginLibrary.h and
	VSTPluginInstance.h, exactly as VSTPluginInstance.cpp does. Those headers
	pull in the VST2 ABI (vst/aeffectx.h) and the Steinberg pluginterfaces/
	vst headers (ibstream, ivstaudioprocessor, ivsteditcontroller, ivstevents,
	ivsthostapplication, ivstmessage, ivstparameterchanges, ivstprocesscontext,
	iplugview) that VSTPluginInstance.h needs for its Steinberg::Vst:: pointer
	members. They resolve through $(VST3_SDK), already on this project's include
	path. No additional include directory is required.
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
#include "vst/VSTPluginInstance.h"
#include "filters/VSTPluginCommand.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "filters/loudnessCorrection/VolumeController.h"
#include "Tests/TestHarness.h"

using std::shared_ptr;
using std::unordered_map;
using std::vector;
using std::wstring;

namespace
{
test::Harness harness("VstHostTests");

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

	int customInitialize() override
	{
		++customInitializeCount;
		return FUNCTIONS_MISSING;
	}

private:
	wstring path;
	int customInitializeCount = 0;
};

// Must match TestVst2Plugin.cpp's ChunkBlob layout and magic exactly so we can
// build a chunk the plugin will accept and predict what it emits.
const uint32_t kChunkMagic = 0x32505654u; // 'T','V','P','2' little-endian
const uint32_t kChunkVersion = 1u;

#pragma pack(push, 1)
struct ChunkBlob
{
	uint32_t magic;
	uint32_t version;
	float gain;
	float bypass;
};
#pragma pack(pop)

// Directory of the running test executable. The plugin DLL is copied next to it
// by the HybridConvTests post-build step.
wstring exeDirectory()
{
	wchar_t path[MAX_PATH] = {};
	DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (len == 0 || len >= MAX_PATH)
		return wstring();

	wstring full(path, len);
	size_t slash = full.find_last_of(L"\\/");
	if (slash == wstring::npos)
		return wstring();
	return full.substr(0, slash);
}

bool fileExists(const wstring& path)
{
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Base64-encode a ChunkBlob the way the engine stores chunk state, so it can be
// fed straight into VSTPluginInstance::writeToEffect.
wstring encodeChunk(const ChunkBlob& blob)
{
	DWORD stringLength = 0;
	CryptBinaryToStringW(reinterpret_cast<const BYTE*>(&blob), sizeof(blob),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &stringLength);
	if (stringLength == 0)
		return wstring();

	vector<wchar_t> buffer(stringLength);
	if (CryptBinaryToStringW(reinterpret_cast<const BYTE*>(&blob), sizeof(blob),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, buffer.data(), &stringLength) != TRUE)
		return wstring();
	return wstring(buffer.data());
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

bool closeEnough(double a, double b)
{
	return std::fabs(a - b) <= 1.0e-9;
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
			if (!closeEnough(outLeft[i], inLeft[i]) || !closeEnough(outRight[i], inRight[i]))
				unchanged = false;
		}
		harness.expectTrue(unchanged, label + ": malformed plugin is bypassed");
	}

	SetEnvironmentVariableW(L"EAPO_TEST_VST_METADATA", nullptr);
}

void testVolumeControllerBalancesComInitialization()
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
} // namespace

void runVstHostTests()
{
	testVolumeControllerBalancesComInitialization();

	// Both soft-skip paths report before returning: under the harness default
	// (Collect) a failure recorded above only fails the build through report().
	const wstring dir = exeDirectory();
	if (dir.empty())
	{
		std::printf("VstHostTests skipped: could not resolve test executable directory\n");
		harness.report();
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
		harness.report();
		return;
	}

	// A failed subclass initialization must roll the DLL load back completely.
	// Otherwise the second call sees a non-null module and returns 0 (already
	// initialized), turning the original failure into a false success.
	RejectingLibrary rejectingLibrary(dllPath);
	harness.expectEqual(rejectingLibrary.initialize(), AbstractLibrary::FUNCTIONS_MISSING,
		"custom initialization failure is reported");
	harness.expectEqual(rejectingLibrary.initialize(), AbstractLibrary::FUNCTIONS_MISSING,
		"custom initialization failure is reported again after rollback");
	harness.expectEqual(rejectingLibrary.getCustomInitializeCount(), 2,
		"custom initialization is retried after rollback");

	// Load the library through the engine's loader (LoadLibrary +
	// GetProcAddress(VSTPluginMain)). initialize() returns >0 on the first
	// successful load (1) and 0 if already loaded; negative values are the
	// AbstractLibrary error codes.
	const VSTPluginCommand importedCommand = VSTPluginCommand::parse(
		L"", L"Library \"" + dllPath + L"\"");
	harness.expectTrue(importedCommand.libraryPath == dllPath,
		"imported config retains the external VST library path");
	shared_ptr<VSTPluginLibrary> library =
		VSTPluginLibrary::getInstance(importedCommand.libraryPath);
	harness.require(library != nullptr, "getInstance returned a library");
	harness.expectFalse(library->isVST3(), "test plugin is hosted via the VST2 path");

	int loadResult = library->initialize();
	harness.expectTrue(loadResult >= 0, "library initialize did not return an error code");
	harness.expectTrue(library->VSTPluginMain != nullptr, "VSTPluginMain symbol resolved");

	const wstring disguisedVst2Path = dir + L"\\TestVst2Disguised.vst3";
	if (CopyFileW(dllPath.c_str(), disguisedVst2Path.c_str(), FALSE) != FALSE)
	{
		shared_ptr<VSTPluginLibrary> disguisedLibrary = VSTPluginLibrary::getInstance(disguisedVst2Path);
		harness.expectTrue(disguisedLibrary->initialize() >= 0 && !disguisedLibrary->isVST3(),
			"loaded module ABI recognizes VST2 even when the file extension is .vst3");

		VSTPluginFilterFactory factory;
		wstring busCommand = L"VST3Bus";
		wstring busParameters = L"Library \"" + disguisedVst2Path
			+ L"\" Input Stereo Output 7.1";
		FilterVector rejected = factory.createFilter(L"test-vst3bus.txt", busCommand, busParameters);
		harness.expectTrue(rejected.empty(),
			"VST3Bus rejects a loaded VST2 module without creating a processing filter");
		harness.expectTrue(busCommand == L"VST3Bus",
			"VST3Bus rejection never rewrites the command to VSTPlugin");
	}
	else
		harness.expectTrue(false, "VST2 disguised-extension copy is available for ABI detection");

	expectRejectedMetadataPassesThrough(library, L"huge-inputs", "unrealistic input count");
	expectRejectedMetadataPassesThrough(library, L"negative-inputs", "negative input count");
	expectRejectedMetadataPassesThrough(library, L"huge-outputs", "unrealistic output count");
	expectRejectedMetadataPassesThrough(library, L"negative-outputs", "negative output count");
	expectRejectedMetadataPassesThrough(library, L"negative-delay", "negative initial delay");
	expectRejectedMetadataPassesThrough(library, L"huge-delay", "unrealistic initial delay");

	// Construct and initialize the instance the way the engine does (heap
	// allocated, owned here). processLevel mirrors a realtime audio thread.
	auto instance = std::make_unique<VSTPluginInstance>(library, 2);
	bool initialized = instance->initialize();
	harness.expectTrue(initialized, "VSTPluginInstance initialize succeeded");

	harness.expectEqual(instance->numInputs(), 2, "plugin reports 2 inputs");
	harness.expectEqual(instance->numOutputs(), 2, "plugin reports 2 outputs");
	harness.expectTrue(instance->canReplacing(), "plugin advertises float replacing");
	harness.expectTrue(instance->canDoubleReplacing(), "plugin advertises double replacing");
	harness.expectTrue(instance->getName() == L"TestVst2Plugin", "plugin reports its name");

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
	harness.expectTrue(closeEnough(defaultBlob.gain, 1.0), "default gain is unity");

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
	harness.expectTrue(closeEnough(readBlob.gain, testGain), "gain survived the chunk write/read round-trip");
	harness.expectTrue(chunkB == writeChunk, "chunk string is stable after writing the same state");

	// Re-reading without an intervening write must return the identical string.
	wstring chunkC;
	unordered_map<wstring, float> paramsC;
	instance->readFromEffect(chunkC, paramsC);
	harness.expectTrue(chunkC == chunkB, "consecutive readFromEffect calls are stable");

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
	instance->processDoubleReplacing(inArray, outArray, frameCount);

	bool audioMatches = true;
	for (int i = 0; i < frameCount && audioMatches; ++i)
	{
		if (!closeEnough(outLeft[i], inLeft[i] * testGain) || !closeEnough(outRight[i], inRight[i] * testGain))
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
	instance->processDoubleReplacing(inArray, unityOutArray, frameCount);

	bool unityMatches = true;
	for (int i = 0; i < frameCount && unityMatches; ++i)
	{
		if (!closeEnough(unityOutLeft[i], inLeft[i]) || !closeEnough(unityOutRight[i], inRight[i]))
			unityMatches = false;
	}
	harness.expectTrue(unityMatches, "unity gain passes audio through unchanged");

	instance->stopProcessing();

	// The owning pointer mirrors the engine and sends effClose on every exit,
	// including an unexpected exception from a later assertion.

	harness.report();
}
