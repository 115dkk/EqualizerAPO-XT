// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
// After VSTPluginInstance.h: the VST3 SDK headers define VST_VERSION as a
// macro, which breaks the VST2 aeffectx.h that header includes first.
#include "vst/VST3HostObjects.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "Tests/TestHarness.h"
#include "VST3/SubwooferRouting/parameter_table.h"
#include "platform/windows/WindowsPath.h"
#include "VST3/SubwooferRouting/plugin_ids.h"

using std::shared_ptr;
using std::wstring;

namespace
{

test::Harness harness("SubwooferRoutingVst3Tests");

using pathutil::exeDirectory;

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

// The smallest host that lets the two halves talk: IConnectionPoint messages
// are IMessage objects the host manufactures.
class MessageHost : public VST3RefCounted<Steinberg::Vst::IHostApplication>
{
public:
	Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override
	{
		wcsncpy_s(reinterpret_cast<wchar_t*>(name), 128, L"SubwooferRoutingVst3Tests", _TRUNCATE);
		return Steinberg::kResultOk;
	}

	Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID cid, Steinberg::TUID iid, void** obj) override
	{
		return VST3HostObjects::createInstance(cid, iid, obj);
	}
};

// The controller's normalization of the trim parameter (-40..0 dB).
double normalizedTrim(double trimDb)
{
	return (trimDb - -40.0) / (0.0 - -40.0);
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

void testParameterTable()
{
	using namespace eapoxt::subwooferrouting::vst3;
	using namespace subroute;

	const PresetCreateResult preset =
		createBuiltInPreset(kIssue246FrontRear41PresetId);
	harness.require(preset.succeeded(), "parameter-table preset is created");

	SubwooferRoutingState fixture = *preset.state;
	fixture.headroom.mode = HeadroomMode::Manual;
	fixture.headroom.manualTrimDb = -12.5;
	for (Path& path : fixture.paths)
	{
		if (path.kind != PathKind::SourceLfe)
			continue;
		path.preGainDb = 6.0;
		for (PathStage& stage : path.chain)
		{
			if (PolarityStage* polarity = std::get_if<PolarityStage>(&stage))
				polarity->inverted = true;
			else if (DelayStage* delay = std::get_if<DelayStage>(&stage))
				delay->milliseconds = 37.5;
		}
	}

	bool fixtureBypass = true;
	for (std::size_t slot = 0; slot < kParameterCount; ++slot)
	{
		const ParameterDescriptor* parameter = parameterBySlot(slot);
		harness.require(parameter != nullptr, "every parameter slot resolves");
		harness.expectEqual(parameter->slot, slot, "parameter slot is stable");

		SubwooferRoutingState roundTrip = fixture;
		bool roundTripBypass = fixtureBypass;
		double normalized = 0.0;
		harness.require(readNormalizedParameter(
			*parameter, fixture, -12.5, fixtureBypass, normalized),
			"parameter reads from the fixture state");
		harness.expectTrue(writeNormalizedParameter(
			*parameter, roundTrip, normalized, -12.5, roundTripBypass),
			"normalized parameter writes back to state");
		const StateEncodeResult fixtureEncoding = encodeStateCanonical(fixture);
		const StateEncodeResult roundTripEncoding = encodeStateCanonical(roundTrip);
		harness.require(fixtureEncoding.succeeded() && roundTripEncoding.succeeded(),
			"parameter round-trip states encode");
		harness.expectTrue(
			*fixtureEncoding.text == *roundTripEncoding.text
				&& fixtureBypass == roundTripBypass,
			"state to normalized to state preserves the full state");

		harness.expectTrue(
			normalizedParameterToPlain(*parameter, 0.0) == parameter->minimum,
			"parameter minimum maps from zero");
		harness.expectTrue(
			normalizedParameterToPlain(*parameter, 1.0) == parameter->maximum,
			"parameter maximum maps from one");
		harness.expectTrue(
			plainParameterToNormalized(*parameter, parameter->minimum) == 0.0,
			"parameter minimum maps to zero");
		harness.expectTrue(
			plainParameterToNormalized(*parameter, parameter->maximum) == 1.0,
			"parameter maximum maps to one");
	}

	const ParameterDescriptor* bypass = parameterById(kBypassParamId);
	const ParameterDescriptor* polarity = parameterById(kSourceLfePolarityParamId);
	const ParameterDescriptor* gain = parameterById(kSourceLfeGainParamId);
	const ParameterDescriptor* delay = parameterById(kSourceLfeDelayParamId);
	const ParameterDescriptor* trim = parameterById(kOutputTrimParamId);
	const ParameterDescriptor* headroom = parameterById(kHeadroomAutoParamId);
	harness.require(bypass && polarity && gain && delay && trim && headroom,
		"all six parameter IDs resolve");
	harness.expectTrue(displayParameterValue(*bypass, 1.0) == L"On",
		"bypass display string is On");
	harness.expectTrue(displayParameterValue(*polarity, 0.0) == L"Normal",
		"polarity display string is Normal");
	harness.expectTrue(displayParameterValue(*gain, 0.75) == L"10.00 dB",
		"gain display string includes dB");
	harness.expectTrue(displayParameterValue(*delay, 0.125) == L"12.50 ms",
		"delay display string includes ms");
	harness.expectTrue(displayParameterValue(*headroom, 1.0) == L"Auto",
		"headroom display string is Auto");

	SubwooferRoutingState trimState = fixture;
	trimState.headroom.mode = HeadroomMode::Auto;
	bool trimBypass = false;
	const double automaticTrimDb = -12.0;
	const double automaticNormalized =
		plainParameterToNormalized(*trim, automaticTrimDb);
	harness.expectTrue(writeNormalizedParameter(
		*trim, trimState, automaticNormalized, automaticTrimDb, trimBypass),
		"automatic trim value writes");
	harness.expectTrue(trimState.headroom.mode == HeadroomMode::Auto,
		"moving trim onto the automatic value keeps Auto");
	harness.expectTrue(writeNormalizedParameter(
		*trim, trimState, automaticNormalized + 0.1, automaticTrimDb, trimBypass),
		"manual trim value writes");
	harness.expectTrue(trimState.headroom.mode == HeadroomMode::Manual,
		"moving trim away from the automatic value switches to Manual");
}

void testStateFrame()
{
	using namespace eapoxt::subwooferrouting::vst3;

	const std::string expected = R"({"state":"parameter table"})";
	std::vector<std::uint8_t> frame;
	const bool written = writeStateFrame(
		[&frame](const void* source, std::size_t size)
		{
			const std::uint8_t* bytes = static_cast<const std::uint8_t*>(source);
			frame.insert(frame.end(), bytes, bytes + size);
			return true;
		},
		expected);
	harness.require(written, "state frame writes");

	auto readFrame = [&frame](std::string& json)
	{
		std::size_t offset = 0;
		return readStateFrame(
			[&frame, &offset](void* destination, std::size_t size)
			{
				if (size > frame.size() - offset)
					return false;
				std::memcpy(destination, frame.data() + offset, size);
				offset += size;
				return true;
			},
			json);
	};

	std::string actual;
	harness.expectTrue(readFrame(actual) && actual == expected,
		"state frame round-trips");

	std::vector<std::uint8_t> badMagic = frame;
	badMagic[0] ^= 0xffU;
	std::size_t badOffset = 0;
	harness.expectFalse(readStateFrame(
		[&badMagic, &badOffset](void* destination, std::size_t size)
		{
			if (size > badMagic.size() - badOffset)
				return false;
			std::memcpy(destination, badMagic.data() + badOffset, size);
			badOffset += size;
			return true;
		},
		actual),
		"state frame rejects bad magic");

	std::vector<std::uint8_t> shortFrame(frame.begin(), frame.end() - 1);
	std::size_t shortOffset = 0;
	harness.expectFalse(readStateFrame(
		[&shortFrame, &shortOffset](void* destination, std::size_t size)
		{
			if (size > shortFrame.size() - shortOffset)
				return false;
			std::memcpy(destination, shortFrame.data() + shortOffset, size);
			shortOffset += size;
			return true;
		},
		actual),
		"state frame rejects a short payload");
}

}

void runSubwooferRoutingVst3Tests()
{
	testParameterTable();
	testStateFrame();

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
	harness.require(instance.negotiateChannelCount(5, channels),
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
	reference.prepare(*compiled.graph);

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

	// Audit #348 C6: the controller's headroom preview compiles at the rate
	// the processor runs at, which the processor reports over the connection
	// once setupProcessing() accepts it (it used to be a fixed 48 kHz).
	{
		const PresetCreateResult rateFixture =
			createBuiltInPreset(kIssue246FrontRear41PresetId);
		harness.require(rateFixture.succeeded()
			&& rateFixture.state->headroom.mode == HeadroomMode::Auto,
			"the controller's default preset uses automatic headroom");
		const CompileResult at48 = compile(*rateFixture.state,
			previewSpecFor(*rateFixture.state, 48000.0));
		const CompileResult at96 = compile(*rateFixture.state,
			previewSpecFor(*rateFixture.state, 96000.0));
		harness.require(at48.headroom.has_value() && at96.headroom.has_value(),
			"the preset compiles at 48 and 96 kHz");
		harness.require(normalizedTrim(at48.headroom->appliedTrimDb)
				!= normalizedTrim(at96.headroom->appliedTrimDb),
			"the preset's automatic trim differs between 48 and 96 kHz");

		IPtr<MessageHost> host = IPtr<MessageHost>::adopt(new MessageHost());

		TUID controllerIid;
		IEditController::iid.toTUID(controllerIid);
		IComponent* rawComponent = nullptr;
		IEditController* rawController = nullptr;
		library->getFactory()->createInstance(
			kComponentCid, componentIid, reinterpret_cast<void**>(&rawComponent));
		library->getFactory()->createInstance(
			kControllerCid, controllerIid, reinterpret_cast<void**>(&rawController));
		IPtr<IComponent> component = IPtr<IComponent>::adopt(rawComponent);
		IPtr<IEditController> controller = IPtr<IEditController>::adopt(rawController);
		harness.require(component && controller, "component and controller are created");
		harness.require(component->initialize(host.get()) == kResultOk
			&& controller->initialize(host.get()) == kResultOk,
			"component and controller initialize against the message host");

		FUnknownPtr<IConnectionPoint> componentPoint(component);
		FUnknownPtr<IConnectionPoint> controllerPoint(controller);
		FUnknownPtr<IAudioProcessor> audio(component);
		harness.require(componentPoint && controllerPoint && audio,
			"both halves expose IConnectionPoint and the component IAudioProcessor");
		componentPoint->connect(controllerPoint);
		controllerPoint->connect(componentPoint);

		harness.expectTrue(controller->getParamNormalized(kOutputTrimParamId)
				== normalizedTrim(at48.headroom->appliedTrimDb),
			"before setupProcessing the controller previews at 48 kHz");

		ProcessSetup setup{};
		setup.processMode = kRealtime;
		setup.symbolicSampleSize = kSample64;
		setup.maxSamplesPerBlock = 1024;
		setup.sampleRate = 96000.0;
		harness.expectTrue(audio->setupProcessing(setup) == kResultOk,
			"the processor accepts a 96 kHz setup");
		harness.expectTrue(controller->getParamNormalized(kOutputTrimParamId)
				== normalizedTrim(at96.headroom->appliedTrimDb),
			"after a 96 kHz setup the controller's trim is the 96 kHz compile's");

		componentPoint->disconnect(controllerPoint);
		controllerPoint->disconnect(componentPoint);
		controller->terminate();
		component->terminate();
	}

	harness.report();
}
