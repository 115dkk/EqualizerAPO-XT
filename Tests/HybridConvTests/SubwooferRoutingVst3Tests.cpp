// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/Processor.h"
#include "SubwooferRouting/StateCodec.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "Tests/TestHarness.h"
#include "VST3/SubwooferRouting/plugin_ids.h"

using std::shared_ptr;
using std::wstring;

namespace
{

test::Harness harness("SubwooferRoutingVst3Tests");

wstring exeDirectory()
{
	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return {};
	const wstring full(path, length);
	const size_t slash = full.find_last_of(L"\\/");
	return slash == wstring::npos ? wstring() : full.substr(0, slash);
}

bool ensureDirectory(const wstring& path)
{
	return CreateDirectoryW(path.c_str(), nullptr) != FALSE
		|| GetLastError() == ERROR_ALREADY_EXISTS;
}

wstring bundleModulePath(const wstring& bundle, const wchar_t* moduleName)
{
	const wstring contents = bundle + L"\\Contents";
#if defined(_M_ARM64)
	const wstring platform = contents + L"\\arm64-win";
#elif defined(_WIN64)
	const wstring platform = contents + L"\\x86_64-win";
#else
	const wstring platform = contents + L"\\x86-win";
#endif
	return platform + L"\\" + moduleName;
}

wstring prepareBundle(
	const wstring& directory,
	const wchar_t* bundleName,
	const wchar_t* moduleName)
{
	const wstring source =
		directory + L"\\EapoXtSubwooferRoutingModule.vst3";
	if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES)
		return {};

	const wstring bundle = directory + L"\\" + bundleName;
	const wstring contents = bundle + L"\\Contents";
	const wstring module = bundleModulePath(bundle, moduleName);
	const size_t slash = module.find_last_of(L"\\/");
	const wstring platform = module.substr(0, slash);

	if (!ensureDirectory(bundle)
		|| !ensureDirectory(contents)
		|| !ensureDirectory(platform))
	{
		return {};
	}

	if (CopyFileW(source.c_str(), module.c_str(), FALSE) == FALSE)
		return {};
	return bundle;
}

wstring encodeChunk(const std::string& json)
{
	constexpr std::uint32_t magic = 0x31584D42;
	const std::uint32_t length = static_cast<std::uint32_t>(json.size());

	std::vector<std::uint8_t> bytes(sizeof(magic) + sizeof(length) + json.size());
	std::memcpy(bytes.data(), &magic, sizeof(magic));
	std::memcpy(bytes.data() + sizeof(magic), &length, sizeof(length));
	if (!json.empty())
	{
		std::memcpy(
			bytes.data() + sizeof(magic) + sizeof(length),
			json.data(),
			json.size());
	}

	DWORD encodedLength = 0;
	CryptBinaryToStringW(
		bytes.data(),
		static_cast<DWORD>(bytes.size()),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
		nullptr,
		&encodedLength);
	if (encodedLength == 0)
		return {};

	std::vector<wchar_t> encoded(encodedLength);
	if (CryptBinaryToStringW(
		bytes.data(),
		static_cast<DWORD>(bytes.size()),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
		encoded.data(),
		&encodedLength) == FALSE)
	{
		return {};
	}
	return wstring(encoded.data());
}

double nextNoise(std::uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	const double normalized =
		static_cast<double>(state) / static_cast<double>(UINT32_MAX);
	return (normalized * 2.0 - 1.0) * 0.25;
}

bool below(const double output[5][64], double threshold)
{
	for (int channel = 0; channel < 5; ++channel)
	{
		for (int sample = 0; sample < 64; ++sample)
		{
			if (std::fabs(output[channel][sample]) > threshold)
				return false;
		}
	}
	return true;
}

}

void runSubwooferRoutingVst3Tests()
{
	using namespace subroute;
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	using namespace eapoxt::subwooferrouting::vst3;

	const wstring directory = exeDirectory();
	const wstring bundle = directory.empty()
		? wstring()
		: prepareBundle(
			directory,
			L"EapoXtSubwooferRouting.vst3",
			L"EapoXtSubwooferRouting.vst3");

	if (bundle.empty())
	{
		harness.expectFalse(bundle.empty(), "Subwoofer Routing VST3 module is staged");
		harness.report();
		return;
	}

	shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(bundle);
	harness.require(library != nullptr, "Subwoofer Routing bundle resolves");
	harness.expectTrue(library->isVST3(), "library is recognized as VST3");
	harness.expectTrue(library->initialize() >= 0, "module initializes");
	harness.require(library->getFactory() != nullptr, "factory is available");

	TUID componentIid;
	IComponent::iid.toTUID(componentIid);
	IComponent* directComponent = nullptr;
	const tresult created = library->getFactory()->createInstance(
		eapoxt::subwooferrouting::vst3::kComponentCid,
		componentIid,
		reinterpret_cast<void**>(&directComponent));
	harness.expectTrue(created == kResultOk && directComponent != nullptr,
		"registered component class is found");

	IAudioProcessor* directAudio = nullptr;
	if (directComponent != nullptr)
	{
		directComponent->queryInterface(
			IAudioProcessor::iid,
			reinterpret_cast<void**>(&directAudio));
	}
	harness.expectTrue(directAudio != nullptr
		&& directAudio->canProcessSampleSize(kSample32) == kResultOk,
		"float32 processing is supported");
	harness.expectTrue(directAudio != nullptr
		&& directAudio->canProcessSampleSize(kSample64) == kResultOk,
		"float64 processing is supported");
	if (directAudio != nullptr)
		directAudio->release();
	if (directComponent != nullptr)
		directComponent->release();

	VSTPluginInstance instance(library, 2);
	harness.require(instance.initialize(), "plugin instance initializes");
	harness.expectTrue(instance.canDoubleReplacing(), "host negotiates double processing");

	const std::vector<wstring> channels = {
		L"L", L"R", L"LFE", L"RL", L"RR"
	};
	instance.setChannelNameHints(channels);
	harness.require(instance.negotiateChannelCount(5),
		"semantic 4.1 channel names negotiate k41Music");
	harness.expectEqual(instance.numInputs(), 5, "input bus has five channels");
	harness.expectEqual(instance.numOutputs(), 5, "output bus has five channels");

	const PresetCreateResult preset =
		createBuiltInPreset(kIssue246FrontRear41PresetId);
	harness.require(preset.succeeded(), "Issue #246 preset is created");

	SubwooferRoutingState desired = *preset.state;
	desired.headroom.mode = HeadroomMode::Manual;
	desired.headroom.manualTrimDb = 0.0;

	const StateEncodeResult encoded = encodeStateCanonical(desired);
	harness.require(encoded.succeeded(), "canonical preset state encodes");
	const wstring chunk = encodeChunk(*encoded.text);
	harness.require(!chunk.empty(), "framed component state encodes as host chunk");

	constexpr int blockSize = 64;
	constexpr int blockCount = 12;
	constexpr double sampleRate = 48000.0;

	PrepareSpec specification;
	specification.sampleRate = sampleRate;
	specification.maximumBlockSize = blockSize;
	specification.channelLayout = {"L", "R", "LFE", "RL", "RR"};

	const CompileResult compiled = compile(desired, specification);
	harness.require(compiled.succeeded(), "reference graph compiles");

	subroute::Processor reference;
	reference.prepare(specification, *compiled.graph);

	instance.prepareForProcessing(static_cast<float>(sampleRate), blockSize);
	instance.writeToEffect(chunk, std::unordered_map<wstring, float>());
	instance.startProcessing();

	double maximumDifference = 0.0;
	std::uint32_t randomState = 0x24641u;

	for (int blockIndex = 0; blockIndex < blockCount; ++blockIndex)
	{
		double input[5][blockSize] = {};
		double pluginOutput[5][blockSize] = {};
		double referenceOutput[5][blockSize] = {};
		double* inputPlanes[5] = {};
		double* pluginPlanes[5] = {};
		double* referencePlanes[5] = {};
		const double* referenceInputs[5] = {};

		for (int channel = 0; channel < 5; ++channel)
		{
			inputPlanes[channel] = input[channel];
			pluginPlanes[channel] = pluginOutput[channel];
			referencePlanes[channel] = referenceOutput[channel];
			referenceInputs[channel] = input[channel];

			for (int sample = 0; sample < blockSize; ++sample)
				input[channel][sample] = nextNoise(randomState);
		}

		instance.processDoubleReplacing(
			inputPlanes,
			pluginPlanes,
			blockSize);

		AudioBlock referenceBlock(
			referenceInputs,
			referencePlanes,
			5,
			blockSize);
		reference.process(referenceBlock);

		for (int channel = 0; channel < 5; ++channel)
		{
			for (int sample = 0; sample < blockSize; ++sample)
			{
				maximumDifference = std::max(
					maximumDifference,
					std::fabs(
						pluginOutput[channel][sample]
						- referenceOutput[channel][sample]));
			}
		}
	}

	harness.expectTrue(maximumDifference <= 1.0e-12,
		"VST3 double output matches SubwooferRoutingCore within 1e-12");

	double impulseInput[5][blockSize] = {};
	double impulseOutput[5][blockSize] = {};
	double* impulseInputs[5] = {};
	double* impulseOutputs[5] = {};
	for (int channel = 0; channel < 5; ++channel)
	{
		impulseInputs[channel] = impulseInput[channel];
		impulseOutputs[channel] = impulseOutput[channel];
	}
	// cppcheck-suppress unreadVariable // read through the impulseInputs pointer array
	impulseInput[0][0] = 1.0;
	instance.processDoubleReplacing(impulseInputs, impulseOutputs, blockSize);

	instance.stopProcessing();
	instance.startProcessing();

	double silenceInput[5][blockSize] = {};
	double silenceOutput[5][blockSize] = {};
	double* silenceInputs[5] = {};
	double* silenceOutputs[5] = {};
	for (int channel = 0; channel < 5; ++channel)
	{
		silenceInputs[channel] = silenceInput[channel];
		silenceOutputs[channel] = silenceOutput[channel];
	}
	instance.processDoubleReplacing(silenceInputs, silenceOutputs, blockSize);
	harness.expectTrue(below(silenceOutput, 1.0e-9),
		"deactivate/reactivate resets delay and IIR residue");

	instance.stopProcessing();
	harness.report();
}
