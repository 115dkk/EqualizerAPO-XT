/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    Runtime coverage for the VST3 host boundary. The companion module is built
    from repository source, then copied into a standard Windows .vst3 bundle so
    the public VSTPluginLibrary path exercises bundle resolution and module
    lifecycle rather than loading an implementation detail directly.
*/

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "vst/VSTChunkBase64.h"

#include "vst/VSTPluginLibrary.h"
#include "vst/VSTPluginInstance.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
// After VSTPluginLibrary.h: the VST3 SDK defines a VST_VERSION macro that
// would otherwise break the enum of the same name in the VST2 aeffectx.h.
#include "pluginterfaces/vst/vstspeaker.h"
#include "../TestHarness.h"
#include "Tests/Vst3Bundle.h"
#include "platform/windows/WindowsPath.h"
#include "Tests/TestVst3Plugin/TestVst3Protocol.h"
#include "vst/VST3MemoryStream.h"
#include "vst/VSTStateSemantic.h"
#include "services/logging/Logging.h"
#include "engine/FilterEngine.h"
#include "platform/windows/TextEncoding.h"
#include "Tests/TestDirectory.h"
#include "dsp/MxcsrGuard.h"
#include "platform/windows/StackGuarantee.h"

using std::shared_ptr;
using std::wstring;

namespace
{
test::HarnessAccess harness;

using testvst3::PluginState;

// Existing layout cases drive the same request interface as playback and
// the card; the extra names describe the explicit layouts in these cases.
bool negotiateLayouts(VSTPluginInstance& instance, VST3BusLayout input, VST3BusLayout output,
	int width, const std::vector<wstring>&, const std::vector<wstring>& names)
{
	VSTPluginInstance::NegotiationRequest request;
	request.contract = VST3BusContract{input, output};
	request.channelCount = width;
	request.channelNames = names;
	return instance.negotiate(request).accepted;
}

bool negotiateChannels(VSTPluginInstance& instance, int width, const std::vector<wstring>& names)
{
	VSTPluginInstance::NegotiationRequest request;
	request.channelCount = width;
	request.channelNames = names;
	return instance.negotiate(request).accepted;
}

bool negotiateStereoInput(VSTPluginInstance& instance, int input, int output,
	const std::vector<wstring>&, const std::vector<wstring>& names)
{
	VSTPluginInstance::NegotiationRequest request;
	request.channelCount = output;
	request.channelNames = names;
	request.stereoInput = true;
	const auto result = instance.negotiate(request);
	return result.accepted && result.inputs == input && result.outputs == output;
}

wstring encodeState(const PluginState& state)
{
	wstring encoded;
	vstchunk::encodeBase64(&state, sizeof(state), encoded);
	return encoded;
}

using pathutil::exeDirectory;

// The companion module staged beside the executable, wrapped in a bundle.
wstring prepareBundle(const wstring& directory, const wchar_t* bundleName = L"TestVst3Bundle.vst3",
	const wchar_t* moduleName = L"TestVst3Plugin.vst3")
{
	return test::prepareVst3Bundle(directory, L"TestVst3PluginModule.vst3", bundleName, moduleName);
}

shared_ptr<VSTPluginLibrary> loadScenarioLibrary(const wchar_t* bundleName = L"TestVst3Bundle.vst3",
	const wchar_t* moduleName = L"TestVst3Plugin.vst3")
{
	const wstring bundle = prepareBundle(exeDirectory(), bundleName, moduleName);
	auto loaded = VSTPluginLibrary::load(bundle);
	harness.require(!bundle.empty() && loaded.succeeded(), "scenario VST3 module loads");
	return loaded.library;
}

void runUpmixerFilter(const shared_ptr<VSTPluginLibrary>& upmixerLibrary, bool stereoInput, double left, double right, double (&outputData)[8][4])
{
	VSTPluginFilter upmixerFilter(upmixerLibrary, wstring(), std::unordered_map<wstring, float>(), stereoInput);
	std::vector<wstring> surroundChannels = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
	upmixerFilter.initialize(48000.0f, 4, surroundChannels);

	constexpr unsigned frameCount = 4;
	double inputData[8][frameCount] = {};
	double* inputs[8];
	double* outputs[8];
	for (int channel = 0; channel < 8; ++channel)
	{
		inputs[channel] = inputData[channel];
		outputs[channel] = outputData[channel];
	}
	for (unsigned sample = 0; sample < frameCount; ++sample)
	{
		inputData[0][sample] = left;
		inputData[1][sample] = right;
	}
	upmixerFilter.process(outputs, inputs, frameCount);
}


struct UpmixerScenario
{
	using CountFunc = int (*)();
	shared_ptr<VSTPluginLibrary> library;
	CountFunc componentCount = nullptr;
	CountFunc processCount = nullptr;
};

UpmixerScenario loadUpmixerScenario()
{
	const wstring directory = exeDirectory();
	// Multichannel bus negotiation. The companion module in "Upmixer.vst3"
	// mode reproduces the OpenSpatial Upmixer contract measured in PR #213:
	// it accepts stereo, symmetric 7.1, and a stereo input bus feeding a 7.1
	// output bus, but its upmix engine only runs on that last, DAW-style
	// asymmetric layout. On a symmetric 7.1 layout it passes the front pair
	// through and leaves the other six channels silent - the "only the front
	// speakers play" failure reported by users.
	const wstring upmixerBundle = prepareBundle(directory, L"UpmixerBundle.vst3", L"TestVst3Upmixer.vst3");
	shared_ptr<VSTPluginLibrary> upmixerLibrary = VSTPluginLibrary::getInstance(upmixerBundle);
	harness.expectTrue(!upmixerBundle.empty() && VSTPluginLibrary::load(upmixerLibrary->getLibPath()).succeeded(),
		"upmixer VST3 module initializes");

	typedef int (*UpmixerCountFunc)();
	HMODULE upmixerModule = GetModuleHandleW(L"TestVst3Upmixer.vst3");
	UpmixerCountFunc upmixerComponentCount = upmixerModule != nullptr
		? reinterpret_cast<UpmixerCountFunc>(GetProcAddress(upmixerModule, "GetUpmixerComponentCount")) : nullptr;
	UpmixerCountFunc upmixerProcessCount = upmixerModule != nullptr
		? reinterpret_cast<UpmixerCountFunc>(GetProcAddress(upmixerModule, "GetUpmixerProcessCount")) : nullptr;
	harness.expectTrue(upmixerComponentCount != nullptr, "upmixer instance counter is exported");
	harness.expectTrue(upmixerProcessCount != nullptr, "upmixer process counter is exported");

	return {upmixerLibrary, upmixerComponentCount, upmixerProcessCount};
}

struct SurroundScenario
{
	using ArrangementFunc = unsigned long long (*)();
	shared_ptr<VSTPluginLibrary> library;
	ArrangementFunc acceptedArrangement = nullptr;
};

SurroundScenario loadSurroundScenario()
{
	const wstring directory = exeDirectory();
	// Semantic 4.1/5.0 negotiation and accepted-arrangement channel mapping.
	const wstring surround41Bundle = prepareBundle(directory,
		L"Surround41Bundle.vst3", L"TestVst3Surround41.vst3");
	shared_ptr<VSTPluginLibrary> surround41Library = VSTPluginLibrary::getInstance(surround41Bundle);
	harness.expectTrue(!surround41Bundle.empty() && VSTPluginLibrary::load(surround41Library->getLibPath()).succeeded(),
		"Surround41 VST3 module initializes");

	typedef unsigned long long (*Surround41ArrangementFunc)();
	HMODULE surround41Module = GetModuleHandleW(L"TestVst3Surround41.vst3");
	Surround41ArrangementFunc surround41AcceptedArrangement = surround41Module != nullptr
		? reinterpret_cast<Surround41ArrangementFunc>(
			GetProcAddress(surround41Module, "GetSurround41AcceptedOutputArrangement")) : nullptr;
	harness.expectTrue(surround41AcceptedArrangement != nullptr,
		"Surround41 accepted-arrangement probe is exported");

	return {surround41Library, surround41AcceptedArrangement};
}

TEST_CASE(Vst3HostTests, testAutomaticUpmixerVerdictMatchesEngine)
{
	const wstring bundle = prepareBundle(exeDirectory(), L"AuditUpmixerBundle.vst3", L"DeclaredUpmixer.vst3");
	auto library = VSTPluginLibrary::getInstance(bundle);
	harness.require(VSTPluginLibrary::load(bundle).succeeded(), "audit upmixer fixture loads");
	auto engine = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	auto card = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
	harness.require(engine->initialize() && card->initialize(), "both upmixer instances initialize");
	const std::vector<wstring> names = {L"L", L"R", L"C", L"SUB", L"RL", L"RR", L"SL", L"SR"};
	VSTPluginInstance::NegotiationRequest engineRequest;
	engineRequest.channelCount = 8;
	engineRequest.channelNames = names;
	const auto engineLayout = engine->negotiate(engineRequest);
	VSTPluginInstance::NegotiationRequest cardRequest;
	cardRequest.channelCount = 8;
	cardRequest.channelNames = names;
	const auto cardLayout = card->negotiate(cardRequest);
	harness.expectTrue(engineLayout.accepted && cardLayout.accepted, "both automatic requests are accepted");
	harness.expectEqual(engineLayout.inputs, 2, "declared upmixer gets stereo input without a written contract");
	harness.expectTrue(engineLayout.inputLayout == cardLayout.inputLayout
		&& engineLayout.outputLayout == cardLayout.outputLayout, "card and engine accept the same arrangements");
	harness.expectEqual(card->numInputs(), engine->numInputs(),
		"automatic upmixer card verdict has the engine input width (audit #467 TD-19)");
	harness.expectEqual(card->numOutputs(), engine->numOutputs(),
		"automatic upmixer card verdict has the engine output width");
}

TEST_CASE(Vst3HostTests, testForeignClassName)
{
	const wstring bundle = prepareBundle(exeDirectory(), L"FullNameBundle.vst3", testvst3::mode::fullName);
	auto library = VSTPluginLibrary::getInstance(bundle);
	harness.require(VSTPluginLibrary::load(bundle).succeeded(), "unterminated class-name fixture loads");
	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
	harness.require(instance->initialize(), "unterminated class-name fixture initializes");
	harness.expectTrue(instance->getName() == wstring(sizeof(library->getVST3ClassInfo().name), L'N'),
		"an unterminated VST3 class name stops at its array bound (audit #467 TD-52)");
}

TEST_CASE(Vst3HostTests, testStateRestoreFailureIsReported)
{
	for (const wchar_t* mode : {L"NormalRestore.vst3", L"RefuseState.vst3"})
	{
		const wstring bundle = prepareBundle(exeDirectory(), (wstring(L"Audit-") + mode).c_str(), mode);
		auto library = VSTPluginLibrary::getInstance(bundle);
		harness.require(VSTPluginLibrary::load(bundle).succeeded(), "restore refusal fixture loads");
		PluginState saved;
		saved.gain = 0.5;
		const wstring invalidState = wcscmp(mode, L"NormalRestore.vst3") == 0 ? L"V" : encodeState(saved);
		{
			auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
			harness.require(instance->initialize(), "restore refusal instance initializes");
			const auto restored = instance->writeToEffect(invalidState, {});
			harness.expectTrue(!restored && restored.reason != nullptr, "VST3 failed restore returns a reason");
		}
		FILE* logFile = nullptr;
		harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "restore refusal log opens");
		Logging::useStream(logFile, false, true, false);
		{
			PluginState state;
			state.gain = 0.5;
			const wstring chunk = wcscmp(mode, L"NormalRestore.vst3") == 0 ? L"V" : encodeState(state);
			VSTPluginFilter filter(library, chunk, {});
			filter.initialize(48000.0f, 4, {L"L", L"R"});
		}
		std::fflush(logFile);
		std::rewind(logFile);
		wstring log;
		wchar_t text[1024] = {};
		while (std::fgetws(text, 1024, logFile) != nullptr)
			log.append(text);
		Logging::useStream(stdout, true, true, false);
		std::fclose(logFile);
		harness.expectTrue(log.find(L"could not restore its state") != wstring::npos,
			"VST3 decode failure and plug-in state refusal are logged (audit #467 TD-36)");
		harness.expectTrue(log.find(library->getLibPath()) != wstring::npos, "VST3 restore log identifies the library");
	}
}

TEST_CASE(Vst3HostTests, testModeUsesModuleFilenameOnly)
{
	const wstring bundle = prepareBundle(exeDirectory(), L"RejectInit.vst3-parent.vst3", L"Normal.vst3");
	harness.expectTrue(VSTPluginLibrary::load(bundle).succeeded(),
		"a parent directory containing a mode name does not select that mode (audit #467 TD-68)");
}

TEST_CASE(Vst3HostTests, testInitializationRefusalIsNotReportedAsACrash)
{
	const wstring bundle = prepareBundle(exeDirectory(), L"RejectComponentBundle.vst3",
		testvst3::mode::rejectComponent);
	auto library = VSTPluginLibrary::getInstance(bundle);
	harness.require(VSTPluginLibrary::load(bundle).succeeded(), "initialization-refusal fixture loads");
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "initialization-refusal log opens");
	Logging::useStream(logFile, false, true, false);
	{
		VSTPluginFilter filter(library, wstring(), {});
		filter.initialize(48000.0f, 4, {L"L", L"R"});
	}
	std::fflush(logFile);
	std::rewind(logFile);
	wstring log;
	wchar_t text[1024] = {};
	while (std::fgetws(text, 1024, logFile) != nullptr)
		log.append(text);
	Logging::useStream(stdout, true, true, false);
	std::fclose(logFile);
	harness.expectTrue(log.find(L"refused initialization") != wstring::npos,
		"a VST3 initialization refusal is named as a refusal");
	harness.expectTrue(log.find(L"crashed during initialization") == wstring::npos,
		"a VST3 initialization refusal is not called a crash");
	harness.expectTrue(log.find(library->getLibPath()) != wstring::npos,
		"the initialization-refusal log identifies the library");
}

TEST_CASE(Vst3HostTests, testEngineFaultContainment)
{
	for (const wchar_t* mode : {L"FaultProcess.vst3", L"FaultActive.vst3", L"FaultState.vst3"})
	{
		const wstring bundle = prepareBundle(exeDirectory(), (wstring(L"Audit-") + mode).c_str(), mode);
		auto library = VSTPluginLibrary::getInstance(bundle);
		harness.require(VSTPluginLibrary::load(bundle).succeeded(), "VST3 engine fault fixture loads");
		FILE* logFile = nullptr;
		harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "VST3 fault log capture opens");
		Logging::useStream(logFile, false, true, false);
		{
			PluginState state;
			state.gain = 0.5;
			VSTPluginFilter filter(library, encodeState(state), {});
			filter.initialize(48000.0f, 4, {L"L", L"R"});
			double inLeft[] = {0.25, 0.5, 0.75, 1.0}, inRight[] = {-0.25, -0.5, -0.75, -1.0};
			double outLeft[4] = {}, outRight[4] = {};
			double* input[] = {inLeft, inRight};
			double* output[] = {outLeft, outRight};
			for (int block = 0; block < 2; block++)
			{
				filter.process(output, input, 4);
				for (int frame = 0; frame < 4; frame++)
				{
					harness.expectNear(outLeft[frame], inLeft[frame], 0.0, "faulted VST3 passes the current and later blocks through");
					harness.expectNear(outRight[frame], inRight[frame], 0.0, "faulted VST3 preserves both input channels");
				}
			}
			if (wcscmp(mode, L"FaultProcess.vst3") == 0)
				harness.expectEqual(std::ftell(logFile), 0L, "VST3 audio crash report is deferred until teardown");
		}
		std::fflush(logFile);
		std::rewind(logFile);
		wstring log;
		wchar_t text[1024] = {};
		while (std::fgetws(text, 1024, logFile) != nullptr)
			log.append(text);
		Logging::useStream(stdout, true, true, false);
		std::fclose(logFile);
		const wchar_t* marker = wcscmp(mode, L"FaultProcess.vst3") == 0
			? L"crashed during audio processing" : L"crashed while preparing for processing";
		const size_t first = log.find(marker);
		harness.expectTrue(first != wstring::npos && log.find(marker, first + 1) == wstring::npos,
			"VST3 crash is logged once");
		harness.expectTrue(log.find(library->getLibPath()) != wstring::npos, "VST3 crash log identifies the library");
	}
}

TEST_CASE(Vst3HostTests, testNonFiniteOutputContainment)
{
	auto library = loadScenarioLibrary(L"NanOutputBundle.vst3", testvst3::mode::nanOutput);
	FILE* logFile = nullptr;
	harness.require(tmpfile_s(&logFile) == 0 && logFile != nullptr, "non-finite VST3 log capture opens");
	Logging::useStream(logFile, false, true, false);
	{
		PluginState state;
		state.gain = 0.5;
		VSTPluginFilter filter(library, encodeState(state), {});
		filter.initialize(48000.0f, 4, {L"L", L"R"});
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
					"non-finite VST3 output never escapes the filter");
				harness.expectNear(outLeft[frame], inLeft[frame], 0.0,
					"non-finite VST3 passes the current and later blocks through");
				harness.expectNear(outRight[frame], inRight[frame], 0.0,
					"non-finite VST3 preserves every channel");
			}
		}
		harness.expectEqual(std::ftell(logFile), 0L, "non-finite VST3 report is deferred until teardown");
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
		"non-finite VST3 output is reported exactly once");
	harness.expectTrue(log.find(library->getLibPath()) != wstring::npos,
		"non-finite VST3 report identifies the library");
}

TEST_CASE(Vst3HostTests, testStateDependentLatencyDelaysUnwrittenChannel)
{
	auto library = loadScenarioLibrary(L"StateLatencyBundle.vst3", testvst3::mode::stateLatency);
	PluginState state;
	state.gain = 0.5;
	VST3BusContract contract;
	contract.input = VST3BusLayout::Stereo;
	contract.output = VST3BusLayout::Stereo;
	VSTPluginFilter filter(library, encodeState(state), {}, contract,
		{L"L", L"R"}, std::vector<wstring>());
	filter.initialize(48000.0f, 128, {L"L", L"R", L"C"});

	double inputData[3][128] = {};
	// cppcheck-suppress constVariable -- the plugin writes this array through outputs below.
	double outputData[3][128] = {};
	inputData[2][0] = 1.0;
	double* inputs[3] = {inputData[0], inputData[1], inputData[2]};
	double* outputs[3] = {outputData[0], outputData[1], outputData[2]};
	filter.process(outputs, inputs, 128);
	bool delayed = true;
	for (unsigned sample = 0; sample < 128; sample++)
	{
		const double expected = sample == 64 ? 1.0 : 0.0;
		delayed = delayed && outputData[2][sample] == expected;
	}
	harness.expectTrue(delayed,
		"an unwritten VST3 channel uses the 64-sample latency reported after state restore");
}

TEST_CASE(Vst3HostTests, testProcessRestoresFloatingPointState)
{
	const wstring bundle = prepareBundle(exeDirectory(), L"FpStateProcessBundle.vst3",
		testvst3::mode::fpStateProcess);
	auto library = VSTPluginLibrary::getInstance(bundle);
	harness.require(VSTPluginLibrary::load(bundle).succeeded(), "FP-state fixture loads");
	VSTPluginFilter filter(library, wstring(), {});
	filter.initialize(48000.0f, 4, {L"L", L"R"});
	double inLeft[4] = {}, inRight[4] = {}, outLeft[4] = {}, outRight[4] = {};
	double* input[] = {inLeft, inRight};
	double* output[] = {outLeft, outRight};
	const unsigned __int64 before = captureFpControlState();
	filter.process(output, input, 4);
	const unsigned __int64 after = captureFpControlState();
	restoreFpControlState(before);
	harness.expectEqual(after, before, "VST3 process restores the caller's FP control state");
}

TEST_CASE(Vst3HostTests, testConsecutiveStackOverflowsAreContained)
{
#ifdef __SANITIZE_ADDRESS__
	harness.skip("MSVC AddressSanitizer terminates on the intentional plug-in stack overflow before SEH can contain it");
	return;
#else
	// The size of the context Windows writes on an overflow follows this mask;
	// logged so a run that dies here can be matched to the machine it ran on.
	std::printf("enabled XState features: 0x%llx\n", static_cast<unsigned long long>(GetEnabledXStateFeatures()));
	std::fflush(stdout);
	const wstring bundle = prepareBundle(exeDirectory(), L"StackOverflowProcessBundle.vst3",
		testvst3::mode::stackOverflowProcess);
	auto library = VSTPluginLibrary::getInstance(bundle);
	harness.require(VSTPluginLibrary::load(bundle).succeeded(), "stack-overflow fixture loads");
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		VSTPluginFilter filter(library, wstring(), {});
		filter.initialize(48000.0f, 4, {L"L", L"R"});
		double inLeft[] = {0.25, 0.5, 0.75, 1.0}, inRight[] = {-0.25, -0.5, -0.75, -1.0};
		double outLeft[4] = {}, outRight[4] = {};
		double* input[] = {inLeft, inRight};
		double* output[] = {outLeft, outRight};
		filter.process(output, input, 4);
		for (int frame = 0; frame < 4; ++frame)
		{
			harness.expectNear(outLeft[frame], inLeft[frame], 0.0,
				"stack-overflowing VST3 passes the left channel through");
			harness.expectNear(outRight[frame], inRight[frame], 0.0,
				"stack-overflowing VST3 passes the right channel through");
		}
	}
	harness.expectTrue(true, "two consecutive plug-in stack overflows return to the host");
	// Windows writes the processor context onto the overflowing stack before any
	// handler runs; without a guarantee that fits it the process dies on CPUs
	// with large extended state (see platform/windows/StackGuarantee.h).
	ULONG guarantee = 0;
	harness.require(SetThreadStackGuarantee(&guarantee) != FALSE, "the thread stack guarantee can be read");
	harness.expectTrue(guarantee >= pluginCallStackGuaranteeBytes,
		"a plug-in call leaves the calling thread with the stack-overflow guarantee");
#endif
}

TEST_CASE(Vst3HostTests, testProcessingFailureResults)
{
	for (const wchar_t* mode : {L"RejectSetup.vst3", L"RejectActive.vst3", L"RejectProcessing.vst3"})
	{
		const wstring bundle = prepareBundle(exeDirectory(), (wstring(L"Audit-") + mode).c_str(), mode);
		auto library = VSTPluginLibrary::getInstance(bundle);
		harness.require(VSTPluginLibrary::load(bundle).succeeded(), "lifecycle refusal fixture loads");
		auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
		harness.require(instance->initialize(), "lifecycle refusal instance initializes");
		harness.require(instance->negotiate({}).accepted, "lifecycle refusal buses negotiate");
		const bool prepared = instance->prepareForProcessing(48000.0f, 16);
		if (wcscmp(mode, L"RejectSetup.vst3") == 0)
			harness.expectFalse(prepared, "setupProcessing refusal reaches prepareForProcessing");
		else
		{
			harness.require(prepared, "activation refusal fixture prepares");
			harness.expectFalse(instance->startProcessing(), "setActive/setProcessing refusal reaches startProcessing");
		}
		harness.expectFalse(instance->canProcessNow(), "refused processing is never reported ready");
	}
}

wstring encodeBytes(const std::vector<char>& bytes)
{
	wstring encoded;
	vstchunk::encodeBase64(bytes.data(), bytes.size(), encoded);
	return encoded;
}

// The combined state layout VSTPluginInstance writes for a VST3 plug-in
// (magic "E3ST", version 1, three sizes, then component state, controller
// state and the parameter snapshot). Spelled out here because it is a file
// format: a saved configuration must keep loading.
#pragma pack(push, 1)
struct CombinedStateHeader
{
	uint32_t magic = 0x54533345;
	uint32_t version = 1;
	uint32_t componentSize = 0;
	uint32_t controllerSize = 0;
	uint32_t parameterCount = 0;
};

struct CombinedStateParameter
{
	uint32_t id = 0;
	double value = 0.0;
};
#pragma pack(pop)

// Audit #348 TD-48: the stream a plug-in reads and writes its state through
// takes positions from the plug-in, so a wild one must not turn into an
// allocation or a copy past the plug-in's buffer.
TEST_CASE(Vst3HostTests, testMemoryStreamBounds)
{
	using Steinberg::IBStream;
	using Steinberg::int32;
	using Steinberg::int64;
	auto stream = Steinberg::IPtr<VST3MemoryStream>::adopt(new VST3MemoryStream(std::vector<char>{1, 2, 3}));
	int64 position = -1;

	harness.expectEqual(stream->seek(static_cast<int64>(VST3MemoryStream::maximumSize) + 1, IBStream::kIBSeekSet, &position),
		Steinberg::kInvalidArgument, "a seek past the stream bound is refused");
	harness.expectEqual(stream->getData().size(), static_cast<size_t>(3), "a refused seek allocates nothing");
	stream->tell(&position);
	harness.expectEqual(position, static_cast<int64>(0), "a refused seek leaves the cursor in place");

	harness.expectEqual(stream->seek(INT64_MAX, IBStream::kIBSeekEnd, &position), Steinberg::kInvalidArgument,
		"a seek whose sum would overflow is refused");
	harness.expectEqual(stream->seek(2, IBStream::kIBSeekSet, &position), Steinberg::kResultOk,
		"a seek inside the data moves the cursor");
	harness.expectEqual(stream->seek(INT64_MIN, IBStream::kIBSeekCur, &position), Steinberg::kResultOk,
		"a seek before the start clamps");
	harness.expectEqual(position, static_cast<int64>(0), "a seek before the start lands on it");
	harness.expectEqual(stream->seek(0, 7, &position), Steinberg::kInvalidArgument,
		"an unknown seek mode is refused");

	char buffer[4] = {9, 9, 9, 9};
	int32 bytesRead = -1;
	harness.expectEqual(stream->read(buffer, -1, &bytesRead), Steinberg::kResultOk,
		"a negative read count is answered");
	harness.expectTrue(bytesRead == 0 && buffer[0] == 9,
		"a negative read count copies nothing into the plug-in's buffer");

	harness.expectEqual(stream->seek(8, IBStream::kIBSeekSet, &position), Steinberg::kResultOk,
		"a seek past the end within the bound still extends the stream");
	int32 bytesWritten = -1;
	harness.expectEqual(stream->write(buffer, 2, &bytesWritten), Steinberg::kResultOk,
		"a write after the extended end succeeds");
	harness.expectTrue(bytesWritten == 2 && stream->getData().size() == 10
		&& stream->getData()[5] == 0 && stream->getData()[9] == 9,
		"the gap before the write is zero-filled and the write lands after it");
}

// Audit #348 TD-48: a saved state with more parameter values than the
// 1023-slot edit ring must reach the processor whole. The snapshot carries
// 1499 values the component does not use and, last, its gain.
TEST_CASE(Vst3HostTests, testRestoreBeyondEditRing)
{
	const wstring directory = exeDirectory();
	const wstring bundle = prepareBundle(directory, L"ParameterFloodBundle.vst3", L"ParameterFlood.vst3");
	shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(bundle);
	harness.require(!bundle.empty() && VSTPluginLibrary::load(library->getLibPath()).succeeded(), "parameter-flood VST3 module initializes");
	typedef int (*CountFunc)();
	HMODULE module = GetModuleHandleW(L"ParameterFlood.vst3");
	CountFunc receivedChanges = module != nullptr
		? reinterpret_cast<CountFunc>(GetProcAddress(module, "GetReceivedParameterChangeCount")) : nullptr;
	harness.expectTrue(receivedChanges != nullptr, "parameter-change probe is exported");
	if (receivedChanges == nullptr)
		return;

	auto instanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& instance = *instanceOwner;
	harness.require(instance.initialize(), "parameter-flood component initializes");

	constexpr uint32_t parameterCount = 1500;
	PluginState component;
	component.gain = 1.0;
	CombinedStateHeader header;
	header.componentSize = sizeof(component);
	header.parameterCount = parameterCount;
	std::vector<char> blob(sizeof(header) + sizeof(component) + parameterCount * sizeof(CombinedStateParameter));
	memcpy(blob.data(), &header, sizeof(header));
	memcpy(blob.data() + sizeof(header), &component, sizeof(component));
	for (uint32_t i = 0; i < parameterCount; i++)
	{
		CombinedStateParameter parameter;
		// The component's gain parameter id is 100 (TestVst3Plugin).
		parameter.id = i + 1 == parameterCount ? 100 : 1000 + i;
		parameter.value = i + 1 == parameterCount ? 0.625 : 0.5;
		memcpy(blob.data() + sizeof(header) + sizeof(component) + i * sizeof(parameter), &parameter, sizeof(parameter));
	}

	const int changesBefore = receivedChanges();
	instance.writeToEffect(encodeBytes(blob), std::unordered_map<wstring, float>());
	harness.expectEqual(receivedChanges() - changesBefore, static_cast<int>(parameterCount),
		"every restored parameter value reaches the processor");

	PluginState restored;
	restored.gain = 0.625;
	wstring chunk;
	std::unordered_map<wstring, float> parameters;
	instance.readFromEffect(chunk, parameters);
	harness.expectTrue(chunk == encodeState(restored),
		"the value after the 1023rd restored parameter is applied");
}

// A DPF-based plug-in returns from a zero-sample process call before it reads
// the parameter changes. An editor edit must still reach its component before
// the configuration state is saved, or the saved ChunkData keeps the old value.
TEST_CASE(Vst3HostTests, testEditorEditWithoutZeroSampleFlush)
{
	const wstring directory = exeDirectory();
	const wstring bundle = prepareBundle(directory, L"IgnoreParameterFlushBundle.vst3", L"IgnoreParameterFlush.vst3");
	shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(bundle);
	harness.require(!bundle.empty() && VSTPluginLibrary::load(library->getLibPath()).succeeded(),
		"flush-ignoring VST3 module initializes");
	auto instanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& instance = *instanceOwner;
	harness.require(instance.initialize(), "flush-ignoring component initializes");
	instance.prepareForProcessing(48000.0f, 16);

	HWND parent = CreateWindowExW(0, L"STATIC", L"VST3 flush test parent", WS_OVERLAPPED,
		0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	harness.require(parent != nullptr, "flush test parent is created");
	short width = 0;
	short height = 0;
	harness.require(instance.startEditing(parent, &width, &height, 1.0), "flush-ignoring editor attaches");
	HWND hostWindow = GetWindow(parent, GW_CHILD);
	HWND pluginWindow = hostWindow != nullptr ? GetWindow(hostWindow, GW_CHILD) : nullptr;
	harness.require(pluginWindow != nullptr, "flush-ignoring editor creates its child window");
	wstring automatedChunk;
	instance.setAutomateFunc([&instance, &automatedChunk]() {
		std::unordered_map<wstring, float> parameters;
		instance.readFromEffect(automatedChunk, parameters);
	});
	SendMessageW(pluginWindow, WM_APP + 77, 0, 0);
	PluginState edited;
	edited.gain = 0.25;
	harness.expectTrue(automatedChunk == encodeState(edited),
		"an editor edit reaches a component that ignores zero-sample calls before the state is saved");
	instance.stopEditing();
	DestroyWindow(parent);
}

TEST_CASE(Vst3HostTests, testUntouchedVst3PanelsDoNotStoreState)
{
	for (const wchar_t* mode : {testvst3::mode::spuriousDirty,
		testvst3::mode::readOnlyMeter, testvst3::mode::volatileState})
	{
		const wstring bundleName = wstring(L"Untouched-") + mode;
		const wstring bundle = prepareBundle(exeDirectory(), bundleName.c_str(), mode);
		auto library = VSTPluginLibrary::getInstance(bundle);
		harness.require(!bundle.empty() && VSTPluginLibrary::load(bundle).succeeded(),
			"untouched-panel VST3 module initializes");
		auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
		harness.require(instance->initialize(), "untouched-panel instance initializes");

		wstring committedChunk;
		std::unordered_map<wstring, float> committedLegacyParameters;
		instance->readFromEffect(committedChunk, committedLegacyParameters);
		VSTPluginInstance::WritableParameterValues committedParameters;
		const bool semanticAvailable = instance->readWritableParameterValues(committedParameters);
		uint64_t committedSequence = instance->parameterEditSequence();
		int storeCount = 0;
		const auto readAsAutoApply = [&]() {
			wstring chunk;
			std::unordered_map<wstring, float> legacyParameters;
			instance->readFromEffect(chunk, legacyParameters);
			VSTPluginInstance::WritableParameterValues parameters;
			const bool currentSemanticAvailable = instance->readWritableParameterValues(parameters);
			const uint64_t sequence = instance->parameterEditSequence();
			const bool rawChanged = chunk != committedChunk || legacyParameters != committedLegacyParameters;
			if (vststate::shouldStore(rawChanged, semanticAvailable && currentSemanticAvailable,
				committedParameters, parameters, committedSequence, sequence, false))
			{
				storeCount++;
				committedChunk = std::move(chunk);
				committedLegacyParameters = std::move(legacyParameters);
				committedParameters = std::move(parameters);
				committedSequence = sequence;
			}
		};
		instance->setAutomateFunc(readAsAutoApply);
		HWND parent = CreateWindowExW(0, L"STATIC", L"VST3 untouched panel parent", WS_OVERLAPPED,
			0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
		short width = 0;
		short height = 0;
		harness.require(parent != nullptr && instance->startEditing(parent, &width, &height),
			"untouched VST3 editor attaches");
		for (int poll = 0; poll < 3; poll++)
		{
			Sleep(20);
			readAsAutoApply();
		}
		harness.expectEqual(storeCount, 0,
			"an untouched VST3 panel causes no auto-apply state store");
		instance->stopEditing();
		DestroyWindow(parent);
	}
}

TEST_CASE(Vst3HostTests, testReadOnlyPanelParameterIsNotAnEdit)
{
	const wstring bundle = prepareBundle(exeDirectory(), L"ReadOnlyMeterBundle.vst3",
		testvst3::mode::readOnlyMeter);
	auto library = VSTPluginLibrary::getInstance(bundle);
	harness.require(!bundle.empty() && VSTPluginLibrary::load(bundle).succeeded(),
		"read-only meter VST3 module initializes");
	HMODULE module = GetModuleHandleW(testvst3::mode::readOnlyMeter);
	using CountFunc = int (*)();
	CountFunc receivedChanges = module != nullptr
		? reinterpret_cast<CountFunc>(GetProcAddress(module, "GetReceivedParameterChangeCount")) : nullptr;
	harness.require(receivedChanges != nullptr, "read-only meter change counter is exported");
	if (receivedChanges == nullptr)
		return;

	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::User);
	harness.require(instance->initialize(), "read-only meter instance initializes");
	int automateCalls = 0;
	instance->setAutomateFunc([&automateCalls]() { automateCalls++; });
	const int changesBefore = receivedChanges();
	HWND parent = CreateWindowExW(0, L"STATIC", L"VST3 read-only meter parent", WS_OVERLAPPED,
		0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	short width = 0;
	short height = 0;
	harness.require(parent != nullptr && instance->startEditing(parent, &width, &height),
		"read-only meter editor attaches");
	harness.expectEqual(automateCalls, 0, "performEdit for a read-only parameter is not automation");
	harness.expectEqual(receivedChanges(), changesBefore,
		"performEdit for a read-only parameter never reaches the processor");
	instance->stopEditing();
	DestroyWindow(parent);
}

// zAuro Pro 2D-3D Upmixer 0.3.0 has three mono auxiliary buses per direction
// beside its main pair. It answers kResultFalse to a setBusArrangements call
// that names the main buses alone, so an explicit Input/Output contract was
// refused and the automatic negotiation kept the last layout it tried rather
// than the first one the plug-in took.
TEST_CASE(Vst3HostTests, testAuxiliaryBusNegotiation)
{
	const wstring directory = exeDirectory();
	const wstring bundle = prepareBundle(directory, L"AuxBusUpmixerBundle.vst3", L"TestVst3AuxBusUpmixer.vst3");
	shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(bundle);
	harness.require(!bundle.empty() && VSTPluginLibrary::load(library->getLibPath()).succeeded(),
		"auxiliary-bus VST3 module initializes");
	typedef int (*CountFunc)();
	HMODULE module = GetModuleHandleW(L"TestVst3AuxBusUpmixer.vst3");
	CountFunc partialProposalCount = module != nullptr
		? reinterpret_cast<CountFunc>(GetProcAddress(module, "GetAuxBusPartialProposalCount")) : nullptr;
	harness.require(partialProposalCount != nullptr, "partial-proposal counter is exported");

	auto instanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& instance = *instanceOwner;
	harness.require(instance.initialize(), "auxiliary-bus component initializes");
	const std::vector<wstring> stereoNames = vst3BusLayoutChannelNames(VST3BusLayout::Stereo);
	const std::vector<wstring> surround71Names = vst3BusLayoutChannelNames(VST3BusLayout::Surround71);

	harness.expectTrue(negotiateChannels(instance, 8, surround71Names),
		"a plug-in with auxiliary buses negotiates the device width");
	harness.expectEqual(instance.numInputs(), 8, "auxiliary-bus plug-in takes an 8-channel main input");
	harness.expectEqual(instance.numOutputs(), 8, "auxiliary-bus plug-in takes an 8-channel main output");
	harness.expectTrue(negotiateStereoInput(instance, 2, 8, stereoNames, surround71Names),
		"a plug-in with auxiliary buses accepts the stereo-input/7.1-output layout");
	harness.expectTrue(negotiateLayouts(instance, VST3BusLayout::Stereo, VST3BusLayout::Surround71,
		8, stereoNames, surround71Names),
		"an explicit Stereo -> 7.1 contract is accepted by a plug-in with auxiliary buses");
	harness.expectEqual(instance.numInputs(), 2, "explicit contract keeps the main input stereo");
	harness.expectEqual(instance.numOutputs(), 8, "explicit contract keeps the main output at 8 channels");
	harness.expectTrue(partialProposalCount != nullptr && partialProposalCount() == 0,
		"every bus arrangement proposal names the auxiliary buses too");

	// The auxiliary buses stay inactive, so processing still hands over the
	// main pair only.
	instance.prepareForProcessing(48000.0f, 4);
	instance.startProcessing();
	double inputData[2][4] = {};
	for (int sample = 0; sample < 4; sample++)
	{
		inputData[0][sample] = 0.25;
		inputData[1][sample] = 0.5;
	}
	double outputData[8][4] = {};
	double* inputs[2] = {inputData[0], inputData[1]};
	double* outputs[8] = {};
	for (int channel = 0; channel < 8; channel++)
		outputs[channel] = outputData[channel];
	instance.processBlock(inputs, outputs, 4);
	instance.stopProcessing();
	harness.expectNear(outputData[2][3], 0.75, 1e-9, "auxiliary-bus upmixer fills the centre from the stereo input");
	harness.expectNear(outputData[6][3], 0.125, 1e-9, "auxiliary-bus upmixer fills the left side channel");
}

// A factory may implement IPluginFactory3 but decline setHostContext, as the
// Kilohearts factory does. The fixture reads the knob in InitDll, so clear it
// immediately after the library load and before creating the component.
TEST_CASE(Vst3HostTests, testFactoryHostContextNotImplemented)
{
	const wstring directory = exeDirectory();
	SetEnvironmentVariableW(L"EAPO_TEST_VST3_HOST_CONTEXT", L"notimpl");
	const wstring bundle = prepareBundle(directory,
		L"HostContextNotImplBundle.vst3", L"HostContextNotImpl.vst3");
	shared_ptr<VSTPluginLibrary> library = bundle.empty()
		? shared_ptr<VSTPluginLibrary>() : VSTPluginLibrary::getInstance(bundle);
	const auto loadStatus = library != nullptr
		? VSTPluginLibrary::load(library->getLibPath()).status : AbstractLibrary::InitializeResult::LoadingFailed;
	SetEnvironmentVariableW(L"EAPO_TEST_VST3_HOST_CONTEXT", nullptr);

	harness.require(AbstractLibrary::succeeded(loadStatus),
		"a VST3 factory may return kNotImplemented from setHostContext (#455)");
	auto instance = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	harness.expectTrue(instance->initialize(),
		"a component initializes after its factory declines setHostContext (#455)");
}

// Audit #348 TD-48: a plug-in that keeps the host's component handler and
// calls it after the instance is gone must be refused, not let into freed
// memory.
TEST_CASE(Vst3HostTests, testHostContextDetach)
{
	const wstring directory = exeDirectory();
	const wstring bundle = prepareBundle(directory, L"RetainHandlerBundle.vst3", L"RetainHandler.vst3");
	shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(bundle);
	harness.require(!bundle.empty() && VSTPluginLibrary::load(library->getLibPath()).succeeded(), "handler-retaining VST3 module initializes");
	typedef int (*CallFunc)();
	typedef void (*ReleaseFunc)();
	HMODULE module = GetModuleHandleW(L"RetainHandler.vst3");
	CallFunc callRetained = module != nullptr
		? reinterpret_cast<CallFunc>(GetProcAddress(module, "CallRetainedComponentHandler")) : nullptr;
	ReleaseFunc releaseRetained = module != nullptr
		? reinterpret_cast<ReleaseFunc>(GetProcAddress(module, "ReleaseRetainedComponentHandler")) : nullptr;
	harness.expectTrue(callRetained != nullptr && releaseRetained != nullptr, "retained-handler probes are exported");
	if (callRetained == nullptr || releaseRetained == nullptr)
		return;

	int automateCalls = 0;
	{
		auto instanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
		VSTPluginInstance& instance = *instanceOwner;
		harness.require(instance.initialize(), "handler-retaining component initializes");
		instance.setAutomateFunc([&automateCalls]() { automateCalls++; });
		harness.expectEqual(callRetained(), 3, "the handler accepts edit and dirty calls while the instance lives");
		harness.expectTrue(automateCalls > 0, "those calls reach the instance");
	}
	const int automateCallsAtRelease = automateCalls;
	harness.expectEqual(callRetained(), 0, "the kept handler refuses both calls after the instance is gone");
	harness.expectEqual(automateCalls, automateCallsAtRelease, "nothing reaches the released instance");
	releaseRetained();
}
}

TEST_CASE(Vst3HostTests, testModuleDiscoveryAndAbi)
{
	const wstring directory = exeDirectory();
	const wstring bundle = directory.empty() ? wstring() : prepareBundle(directory);
	if (bundle.empty())
	{
		harness.expectFalse(bundle.empty(), "companion VST3 module is available");
return;
	}


	shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(bundle);
	harness.require(library != nullptr, "bundle resolves to a library");
	harness.expectTrue(library->isVST3(), "bundle is recognized as VST3");
	{
		const auto judged = ConfigFileReference::library(L"", bundle, L"");
		harness.require(judged.refusal.empty() && judged.path.leaf() != nullptr,
			"bundle module is discovered and pinned through directory handles");
		harness.expectTrue(VSTPluginLibrary::load(library->getLibPath(), &judged.path).succeeded(), "Windows VST3 module loads while the complete bundle path is pinned");
	}
	harness.expectTrue(library->getFactory() != nullptr, "VST3 factory is available after module initialization");

	const wstring rawVst3Module = directory + L"\\TestVst3RawModule.dll";
	const wstring stagedVst3Module = directory + L"\\TestVst3PluginModule.vst3";
	if (CopyFileW(stagedVst3Module.c_str(), rawVst3Module.c_str(), FALSE) != FALSE)
	{
		shared_ptr<VSTPluginLibrary> rawVst3Library = VSTPluginLibrary::getInstance(rawVst3Module);
		harness.expectTrue(VSTPluginLibrary::load(rawVst3Library->getLibPath()).succeeded() && rawVst3Library->isVST3(),
			"loaded module ABI recognizes VST3 even when the file extension is .dll");
		VSTPluginFilterFactory rawFactory;
		wstring rawCommand = L"VSTPlugin";
		wstring rawParameters = L"Library \"" + rawVst3Module
			+ L"\" Input Stereo Output Stereo";
		harness.expectEqual(rawFactory.createFilter(L"raw-vst3.txt", rawCommand, rawParameters).filters.size(),
			(size_t)1, "VSTPlugin Input/Output accepts a loaded raw .dll VST3 module");
	}
	else
		harness.expectTrue(false, "raw VST3 module copy is available for ABI detection");
}

TEST_CASE(Vst3HostTests, testStateRoundTripAndNativeEditor)
{
	const wstring directory = exeDirectory();
	auto library = loadScenarioLibrary();
	auto instanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& instance = *instanceOwner;
	harness.expectTrue(instance.initialize(), "VST3 component initializes with mandatory host services");
	harness.expectEqual(instance.numInputs(), 2, "VST3 component reports stereo input");
	harness.expectEqual(instance.numOutputs(), 2, "VST3 component reports stereo output");
	harness.expectTrue(instance.prepareForProcessing(48000.0f, 16), "VST3 component prepares double input processing");

	PluginState desired;
	desired.gain = 0.5;
	const wstring encodedState = encodeState(desired);
	harness.expectFalse(encodedState.empty(), "component state encodes for restore");
	instance.prepareForProcessing(48000.0f, 16);
	instance.writeToEffect(encodedState, std::unordered_map<wstring, float>());
	instance.startProcessing();

	double doubleInLeft[] = {0.25, -0.5, 0.75, 1.0};
	double doubleInRight[] = {-1.0, 0.5, -0.25, 0.125};
	double doubleOutLeft[4] = {};
	double doubleOutRight[4] = {};
	double* doubleInputs[] = {doubleInLeft, doubleInRight};
	double* doubleOutputs[] = {doubleOutLeft, doubleOutRight};
	instance.processBlock(doubleInputs, doubleOutputs, 4);
	harness.expectTrue(test::nearlyEqual(doubleOutLeft[2], 0.375, 1.0e-9) && test::nearlyEqual(doubleOutRight[0], -0.5, 1.0e-9),
		"VST3 double processing applies restored state");

	instance.stopProcessing();

	HWND parent = CreateWindowExW(0, L"STATIC", L"VST3 test parent", WS_OVERLAPPED,
		0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	harness.expectTrue(parent != nullptr, "native editor test parent is created");
	short width = 0;
	short height = 0;
	harness.expectTrue(instance.startEditing(parent, &width, &height, 1.5), "VST3 native editor attaches to HWND");
	harness.expectTrue(width == 240 && height == 147, "VST3 editor resize and DPI scaling reach the host frame");
	HWND hostWindow = GetWindow(parent, GW_CHILD);
	HWND pluginWindow = hostWindow != nullptr ? GetWindow(hostWindow, GW_CHILD) : nullptr;
	wchar_t editorTitle[128] = {};
	if (pluginWindow != nullptr)
		GetWindowTextW(pluginWindow, editorTitle, 128);
	harness.expectTrue(pluginWindow != nullptr, "VST3 editor creates its child window");
	harness.expectTrue(wstring(editorTitle).find(L"Gain 0.50") != wstring::npos,
		"VST3 editor receives restored component state");
	harness.expectTrue(wstring(editorTitle).find(L"Scale 1.50") != wstring::npos,
		"VST3 editor receives the host content scale factor");
	int scaledWidth = 0;
	int scaledHeight = 0;
	instance.setSizeWindowFunc([&scaledWidth, &scaledHeight](int w, int h) {
		scaledWidth = w;
		scaledHeight = h;
	});
	harness.expectTrue(instance.setEditorScale(2.0), "attached VST3 editor accepts a monitor scale change");
	GetWindowTextW(pluginWindow, editorTitle, 128);
	harness.expectTrue(scaledWidth == 180 && scaledHeight == 110,
		"VST3 monitor scale change reports the logical editor size (360x220 / 2)");
	harness.expectTrue(wstring(editorTitle).find(L"Scale 2.00") != wstring::npos,
		"attached VST3 editor receives the changed content scale factor");
	instance.setSizeWindowFunc(nullptr);
	wstring automatedChunk;
	int automateCallCount = 0;
	instance.setAutomateFunc([&instance, &automatedChunk, &automateCallCount]() {
		std::unordered_map<wstring, float> automatedParameters;
		instance.readFromEffect(automatedChunk, automatedParameters);
		automateCallCount++;
	});
	SendMessageW(pluginWindow, WM_APP + 77, 0, 0);
	PluginState edited;
	edited.gain = 0.25;
	harness.expectTrue(automatedChunk == encodeState(edited),
		"VST3 editor edits are flushed before configuration state is saved");
	HANDLE flushEntered = CreateEventW(nullptr, TRUE, FALSE, testvst3::flushEnteredEvent);
	HANDLE flushContinue = CreateEventW(nullptr, TRUE, FALSE, testvst3::flushContinueEvent);
	HANDLE concurrentProcessing = CreateEventW(nullptr, TRUE, FALSE, testvst3::concurrentProcessingEvent);
	// Audit #250 F053: the release window is anchored to the actual
	// startProcessing attempt (aboutToStart) instead of free-floating after
	// flushEntered, and the flush-entered precondition is asserted below so
	// a stalled runner fails loudly instead of passing vacuously.
	HANDLE aboutToStart = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	std::thread processingStarter([&instance, flushEntered, aboutToStart]() {
		WaitForSingleObject(flushEntered, 5000);
		SetEvent(aboutToStart);
		instance.startProcessing();
	});
	std::thread flushReleaser([flushEntered, flushContinue, aboutToStart]() {
		if (WaitForSingleObject(flushEntered, 5000) == WAIT_OBJECT_0)
		{
			// Give the blocked startProcessing a moment to engage the lock
			// once we know the attempt is imminent, then release the flush.
			WaitForSingleObject(aboutToStart, 5000);
			Sleep(100);
		}
		SetEvent(flushContinue);
	});
	SendMessageW(pluginWindow, WM_APP + 77, 0, 0);
	processingStarter.join();
	flushReleaser.join();
	harness.expectTrue(flushEntered != nullptr
		&& WaitForSingleObject(flushEntered, 0) == WAIT_OBJECT_0,
		"the stopped-editor flush actually entered (no vacuous pass)");
	harness.expectTrue(flushEntered != nullptr && flushContinue != nullptr && concurrentProcessing != nullptr
		&& WaitForSingleObject(concurrentProcessing, 0) == WAIT_TIMEOUT,
		"stopped-editor flush is serialized with audio processing startup");
	if (aboutToStart != nullptr)
		CloseHandle(aboutToStart);
	if (flushEntered != nullptr)
		CloseHandle(flushEntered);
	if (flushContinue != nullptr)
		CloseHandle(flushContinue);
	if (concurrentProcessing != nullptr)
		CloseHandle(concurrentProcessing);
	const int automateCallsBeforeLiveEdit = automateCallCount;
	SendMessageW(pluginWindow, WM_APP + 79, 0, 0);
	harness.expectEqual(automateCallCount, automateCallsBeforeLiveEdit,
		"live VST3 edits are not saved before the audio block consumes them");
	std::fill_n(doubleOutLeft, 4, 0.0);
	std::fill_n(doubleOutRight, 4, 0.0);
	instance.processBlock(doubleInputs, doubleOutputs, 4);
	harness.expectTrue(test::nearlyEqual(doubleOutLeft[2], 0.5625, 1.0e-9) && test::nearlyEqual(doubleOutRight[0], -0.75, 1.0e-9),
		"VST3 editor parameter edits reach audio processing");
	instance.stopProcessing();
	wstring liveEditChunk;
	std::unordered_map<wstring, float> liveEditParameters;
	instance.readFromEffect(liveEditChunk, liveEditParameters);
	PluginState liveEdited;
	liveEdited.gain = 0.75;
	harness.expectTrue(liveEditChunk == encodeState(liveEdited),
		"live VST3 edit state is current after audio processing");
	const wstring componentOnlyState = liveEditChunk;
	VSTPluginInstance::WritableParameterValues parametersBeforePrivateEdit;
	harness.require(instance.readWritableParameterValues(parametersBeforePrivateEdit),
		"VST3 writable parameter baseline is available");
	const uint64_t sequenceBeforePrivateEdit = instance.parameterEditSequence();
	SendMessageW(pluginWindow, WM_APP + 78, 0, 0);
	wstring componentAndControllerState;
	std::unordered_map<wstring, float> controllerStateParameters;
	instance.readFromEffect(componentAndControllerState, controllerStateParameters);
	harness.expectTrue(!componentAndControllerState.empty() && componentAndControllerState != componentOnlyState,
		"VST3 controller-private state is saved with component state");
	VSTPluginInstance::WritableParameterValues parametersAfterPrivateEdit;
	instance.readWritableParameterValues(parametersAfterPrivateEdit);
	const uint64_t sequenceAfterPrivateEdit = instance.parameterEditSequence();
	harness.expectFalse(vststate::shouldStore(true, true,
		parametersBeforePrivateEdit, parametersAfterPrivateEdit,
		sequenceBeforePrivateEdit, sequenceAfterPrivateEdit, false),
		"non-parameter state without performEdit waits for the closing-panel read");
	harness.expectTrue(vststate::shouldStore(true, true,
		parametersBeforePrivateEdit, parametersAfterPrivateEdit,
		sequenceBeforePrivateEdit, sequenceAfterPrivateEdit, true),
		"the closing-panel read stores non-parameter state without performEdit");
	instance.stopEditing();
	harness.expectFalse(IsWindow(pluginWindow) != FALSE, "VST3 editor child is destroyed on close");
	DestroyWindow(parent);

	auto singleComponentInstanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& singleComponentInstance = *singleComponentInstanceOwner;
	harness.expectTrue(singleComponentInstance.initialize(), "single-component VST3 initializes");
	singleComponentInstance.writeToEffect(encodedState, std::unordered_map<wstring, float>());
	HWND singleParent = CreateWindowExW(0, L"STATIC", L"single-component VST3 parent", WS_OVERLAPPED,
		0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	short singleWidth = 0;
	short singleHeight = 0;
	harness.expectTrue(singleParent != nullptr
		&& singleComponentInstance.startEditing(singleParent, &singleWidth, &singleHeight),
		"single-component VST3 is initialized once and opens its native editor");
	singleComponentInstance.stopEditing();
	DestroyWindow(singleParent);

	const wstring controllerBundle = prepareBundle(directory, L"ControllerStateBundle.vst3", L"ControllerState.vst3");
	shared_ptr<VSTPluginLibrary> controllerLibrary = VSTPluginLibrary::getInstance(controllerBundle);
	harness.expectTrue(!controllerBundle.empty() && VSTPluginLibrary::load(controllerLibrary->getLibPath()).succeeded(),
		"separate VST3 module initializes for controller-state restore");
	auto controllerStateInstanceOwner = VSTPluginInstance::create(controllerLibrary, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& controllerStateInstance = *controllerStateInstanceOwner;
	harness.expectTrue(controllerStateInstance.initialize(),
		"split VST3 controller initializes for private-state restore");
	controllerStateInstance.writeToEffect(componentAndControllerState, std::unordered_map<wstring, float>());
	HWND controllerParent = CreateWindowExW(0, L"STATIC", L"controller-state VST3 parent", WS_OVERLAPPED,
		0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	short controllerWidth = 0;
	short controllerHeight = 0;
	harness.expectTrue(controllerParent != nullptr
		&& controllerStateInstance.startEditing(controllerParent, &controllerWidth, &controllerHeight),
		"VST3 controller-state editor opens after restore");
	HWND controllerHostWindow = GetWindow(controllerParent, GW_CHILD);
	HWND controllerPluginWindow = controllerHostWindow != nullptr ? GetWindow(controllerHostWindow, GW_CHILD) : nullptr;
	wchar_t controllerEditorTitle[128] = {};
	if (controllerPluginWindow != nullptr)
		GetWindowTextW(controllerPluginWindow, controllerEditorTitle, 128);
	harness.expectTrue(wstring(controllerEditorTitle).find(L"Zoom 1.75") != wstring::npos,
		"VST3 controller-private state restores in a new native editor");
	wstring controllerOnlyState;
	std::unordered_map<wstring, float> controllerOnlyParameters;
	controllerStateInstance.readFromEffect(controllerOnlyState, controllerOnlyParameters);
	harness.expectTrue(!controllerOnlyState.empty() && controllerOnlyParameters.empty(),
		"controller state and parameters are saved when component state is unavailable");
	controllerStateInstance.stopEditing();
	DestroyWindow(controllerParent);

	const wstring controllerOnlyRestoreBundle = prepareBundle(directory,
		L"ControllerOnlyRestoreBundle.vst3", L"ControllerOnlyRestore.vst3");
	shared_ptr<VSTPluginLibrary> controllerOnlyRestoreLibrary = VSTPluginLibrary::getInstance(controllerOnlyRestoreBundle);
	harness.expectTrue(!controllerOnlyRestoreBundle.empty() && VSTPluginLibrary::load(controllerOnlyRestoreLibrary->getLibPath()).succeeded(),
		"separate VST3 module initializes for controller-only state restore");
	auto controllerOnlyRestoreInstanceOwner = VSTPluginInstance::create(controllerOnlyRestoreLibrary, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& controllerOnlyRestoreInstance = *controllerOnlyRestoreInstanceOwner;
	harness.expectTrue(controllerOnlyRestoreInstance.initialize(),
		"split VST3 controller initializes for controller-only state restore");
	controllerOnlyRestoreInstance.writeToEffect(controllerOnlyState, std::unordered_map<wstring, float>());
	HWND controllerOnlyParent = CreateWindowExW(0, L"STATIC", L"controller-only VST3 parent", WS_OVERLAPPED,
		0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	short controllerOnlyWidth = 0;
	short controllerOnlyHeight = 0;
	harness.expectTrue(controllerOnlyParent != nullptr
		&& controllerOnlyRestoreInstance.startEditing(controllerOnlyParent, &controllerOnlyWidth, &controllerOnlyHeight),
		"VST3 editor opens after controller-only state restore");
	HWND controllerOnlyHostWindow = GetWindow(controllerOnlyParent, GW_CHILD);
	HWND controllerOnlyPluginWindow = controllerOnlyHostWindow != nullptr
		? GetWindow(controllerOnlyHostWindow, GW_CHILD) : nullptr;
	wchar_t controllerOnlyEditorTitle[128] = {};
	if (controllerOnlyPluginWindow != nullptr)
		GetWindowTextW(controllerOnlyPluginWindow, controllerOnlyEditorTitle, 128);
	harness.expectTrue(wstring(controllerOnlyEditorTitle).find(L"Gain 0.75") != wstring::npos
		&& wstring(controllerOnlyEditorTitle).find(L"Zoom 1.75") != wstring::npos,
		"controller-only VST3 state restores parameters and private GUI state");
	controllerOnlyRestoreInstance.stopEditing();
	DestroyWindow(controllerOnlyParent);
}

TEST_CASE(Vst3HostTests, testFloatProcessing)
{
	auto library = loadScenarioLibrary(L"FloatOnlyBundle.vst3", testvst3::mode::floatOnly);
	auto floatOnlyInstanceOwner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& floatOnlyInstance = *floatOnlyInstanceOwner;
	harness.expectTrue(floatOnlyInstance.initialize() && floatOnlyInstance.prepareForProcessing(48000.0f, 16, VSTPluginInstance::InputWidth::Float),
		"32-bit-only VST3 component initializes with the float processing path");
	floatOnlyInstance.prepareForProcessing(48000.0f, 16);
	std::unordered_map<wstring, float> desiredParameters;
	desiredParameters[L"Gain"] = 0.5f;
	floatOnlyInstance.writeToEffect(L"", desiredParameters);
	floatOnlyInstance.startProcessing();
	float floatInLeft[] = {0.25f, -0.5f, 0.75f, 1.0f};
	float floatInRight[] = {-1.0f, 0.5f, -0.25f, 0.125f};
	float floatOutLeft[4] = {};
	float floatOutRight[4] = {};
	float* floatInputs[] = {floatInLeft, floatInRight};
	float* floatOutputs[] = {floatOutLeft, floatOutRight};
	floatOnlyInstance.processBlock(floatInputs, floatOutputs, 4);
	harness.expectTrue(test::nearlyEqual(floatOutLeft[2], 0.375, 1.0e-9) && test::nearlyEqual(floatOutRight[0], -0.5, 1.0e-9),
		"VST3 float processing applies restored state");
	floatOnlyInstance.stopProcessing();
}

// Issue #478: LoudMax VST3 1.47 answers setProcessing(true) with
// kNotImplemented, which is what the VST3 SDK's own AudioEffect base class
// returns, and processes normally. The host took that answer for a refusal and
// passed the audio through untouched.
TEST_CASE(Vst3HostTests, testUnimplementedSetProcessingStillProcesses)
{
	auto library = loadScenarioLibrary(L"UnimplementedProcessingBundle.vst3", testvst3::mode::unimplementedProcessing);
	auto owner = VSTPluginInstance::create(library, VSTPluginInstance::ProcessLevel::Realtime);
	VSTPluginInstance& instance = *owner;
	harness.require(instance.initialize() && instance.prepareForProcessing(48000.0f, 16, VSTPluginInstance::InputWidth::Float),
		"the plug-in initializes and prepares");
	std::unordered_map<wstring, float> desiredParameters;
	desiredParameters[L"Gain"] = 0.5f;
	instance.writeToEffect(L"", desiredParameters);
	harness.expectTrue(instance.startProcessing(), "a kNotImplemented answer to setProcessing starts processing");
	harness.expectTrue(instance.canProcessNow(), "and the instance reports it can process");
	float inLeft[] = {0.25f, -0.5f, 0.75f, 1.0f};
	float inRight[] = {-1.0f, 0.5f, -0.25f, 0.125f};
	float outLeft[4] = {};
	float outRight[4] = {};
	float* inputs[] = {inLeft, inRight};
	float* outputs[] = {outLeft, outRight};
	instance.processBlock(inputs, outputs, 4);
	harness.expectTrue(test::nearlyEqual(outLeft[2], 0.375, 1.0e-9) && test::nearlyEqual(outRight[0], -0.5, 1.0e-9),
		"the plug-in's gain reaches the audio");
	instance.stopProcessing();
}

TEST_CASE(Vst3HostTests, testComponentInitializationRefusal)
{
	const wstring directory = exeDirectory();
	const wstring rejectedComponentBundle = prepareBundle(directory,
		L"RejectComponentBundle.vst3", L"RejectComponent.vst3");
	shared_ptr<VSTPluginLibrary> rejectedComponentLibrary = VSTPluginLibrary::getInstance(rejectedComponentBundle);
	HANDLE invalidTerminate = CreateEventW(nullptr, TRUE, FALSE, testvst3::invalidTerminateEvent);
	harness.expectTrue(!rejectedComponentBundle.empty() && VSTPluginLibrary::load(rejectedComponentLibrary->getLibPath()).succeeded(),
		"VST3 module remains loadable when its component rejects initialization");
	{
		auto rejectedComponentInstanceOwner = VSTPluginInstance::create(rejectedComponentLibrary, VSTPluginInstance::ProcessLevel::Realtime);
		VSTPluginInstance& rejectedComponentInstance = *rejectedComponentInstanceOwner;
		harness.expectFalse(rejectedComponentInstance.initialize(),
			"VST3 component initialization failures are reported");
	}
	harness.expectTrue(invalidTerminate != nullptr
		&& WaitForSingleObject(invalidTerminate, 0) == WAIT_TIMEOUT,
		"a VST3 component is terminated only after successful initialization");
	if (invalidTerminate != nullptr)
		CloseHandle(invalidTerminate);
}

TEST_CASE(Vst3HostTests, testModuleInitializationRefusal)
{
	const wstring directory = exeDirectory();
	const wstring rejectedBundle = prepareBundle(directory, L"RejectInitBundle.vst3", L"RejectInit.vst3");
	shared_ptr<VSTPluginLibrary> rejectedLibrary = VSTPluginLibrary::getInstance(rejectedBundle);
	harness.expectTrue(!rejectedBundle.empty() && !VSTPluginLibrary::load(rejectedLibrary->getLibPath()).succeeded(),
		"VST3 module initialization failures are reported");
	harness.expectTrue(!VSTPluginLibrary::load(rejectedLibrary->getLibPath()).succeeded(),
		"failed VST3 module initialization remains failed when retried");
}

TEST_CASE(Vst3HostTests, testModuleExit)
{
	const wstring directory = exeDirectory();
	const wstring exitBundle = prepareBundle(directory, L"ExitInitBundle.vst3", L"ExitInit.vst3");
	const wstring exitMarker = test::vst3BundleModulePath(exitBundle, L"ExitInit.vst3") + L".exit";
	{
		shared_ptr<VSTPluginLibrary> exitLibrary = VSTPluginLibrary::getInstance(exitBundle);
		harness.expectTrue(!exitBundle.empty() && VSTPluginLibrary::load(exitLibrary->getLibPath()).succeeded(),
			"successful VST3 module is ready for lifecycle teardown");
	}
	harness.expectTrue(GetFileAttributesW(exitMarker.c_str()) != INVALID_FILE_ATTRIBUTES,
		"VST3 module ExitDll runs before the successful library unloads");
}

TEST_CASE(Vst3HostTests, testUpmixerNegotiation)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;

	{
		auto upmixerProbeOwner = VSTPluginInstance::create(upmixerLibrary, VSTPluginInstance::ProcessLevel::Realtime);
		VSTPluginInstance& upmixerProbe = *upmixerProbeOwner;
		harness.expectTrue(upmixerProbe.initialize(), "upmixer VST3 component initializes");
		const std::vector<wstring> stereoNames = vst3BusLayoutChannelNames(VST3BusLayout::Stereo);
		const std::vector<wstring> surround71Names = vst3BusLayoutChannelNames(VST3BusLayout::Surround71);
		harness.expectTrue(negotiateChannels(upmixerProbe, 8, surround71Names),
			"upmixer negotiates up to a 7.1 bus");
		harness.expectEqual(upmixerProbe.numInputs(), 8, "negotiated upmixer input bus spans 8 channels");
		harness.expectEqual(upmixerProbe.numOutputs(), 8, "negotiated upmixer output bus spans 8 channels");
		harness.expectTrue(negotiateStereoInput(upmixerProbe, 2, 8, stereoNames, surround71Names),
			"upmixer accepts the stereo-input/7.1-output layout");
		harness.expectEqual(upmixerProbe.numInputs(), 2, "asymmetric layout keeps the input bus stereo");
		harness.expectEqual(upmixerProbe.numOutputs(), 8, "asymmetric layout keeps the output bus at 8 channels");

		harness.expectTrue(negotiateLayouts(upmixerProbe, VST3BusLayout::Stereo,
			VST3BusLayout::Surround71, 8, stereoNames, surround71Names),
			"VSTPlugin explicit layout accepts Stereo -> 7.1");
		const std::optional<VST3BusLayout> acceptedInput = upmixerProbe.getNegotiatedVST3InputLayout();
		const std::optional<VST3BusLayout> acceptedOutput = upmixerProbe.getNegotiatedVST3OutputLayout();
		harness.expectTrue(acceptedInput && *acceptedInput == VST3BusLayout::Stereo,
			"accepted VST3 input arrangement is exposed as Stereo for Editor diagnostics");
		harness.expectTrue(acceptedOutput && *acceptedOutput == VST3BusLayout::Surround71,
			"accepted VST3 output arrangement is exposed as 7.1 for Editor diagnostics");
		harness.expectTrue(negotiateLayouts(upmixerProbe, VST3BusLayout::Surround71,
			VST3BusLayout::Surround71, 8, surround71Names, surround71Names),
			"VSTPlugin explicit layout accepts 7.1 -> 7.1");
		harness.expectTrue(negotiateLayouts(upmixerProbe, VST3BusLayout::Surround71,
			VST3BusLayout::Stereo, 8, surround71Names, stereoNames),
			"VSTPlugin explicit layout accepts 7.1 -> Stereo");

		harness.expectFalse(negotiateLayouts(upmixerProbe, VST3BusLayout::Surround41,
			VST3BusLayout::Surround71, 8,
			vst3BusLayoutChannelNames(VST3BusLayout::Surround41), surround71Names),
			"VSTPlugin explicit layout reports an input-layout rejection");
		harness.expectFalse(negotiateLayouts(upmixerProbe, VST3BusLayout::Stereo,
			VST3BusLayout::Surround51, 8,
			stereoNames, vst3BusLayoutChannelNames(VST3BusLayout::Surround51)),
			"VSTPlugin explicit layout reports an output-layout rejection");
	}
}

TEST_CASE(Vst3HostTests, testSymmetricUpmixer)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;
	const auto upmixerComponentCount = scenario.componentCount;

	const double left = 0.5;
	const double right = 0.25;
	const int upmixerInstancesBefore = upmixerComponentCount != nullptr ? upmixerComponentCount() : -1;
	{
		// Without StereoInput the host keeps symmetric buses: a single
		// full-width instance, front pair passed through, engine off. This
		// documents the measured real-plugin behavior rather than a host
		// defect - the layout choice must stay an explicit opt-in.
		double outputData[8][4] = {};
		runUpmixerFilter(upmixerLibrary, false, left, right, outputData);
		harness.expectTrue(test::nearlyEqual(outputData[0][0], left, 1.0e-9) && test::nearlyEqual(outputData[1][0], right, 1.0e-9),
			"symmetric 7.1 layout passes the front pair through");
		harness.expectTrue(test::nearlyEqual(outputData[2][0], 0.0, 1.0e-9) && test::nearlyEqual(outputData[4][0], 0.0, 1.0e-9),
			"symmetric 7.1 layout leaves the upmix engine disengaged");
	}
	if (upmixerComponentCount != nullptr)
		harness.expectEqual(upmixerComponentCount() - upmixerInstancesBefore, 1,
			"a 7.1 device is served by exactly one full-width upmixer instance");
}

TEST_CASE(Vst3HostTests, testStereoInputUpmixer)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;
	const auto upmixerComponentCount = scenario.componentCount;

	const double left = 0.5;
	const double right = 0.25;
	const int stereoInputInstancesBefore = upmixerComponentCount != nullptr ? upmixerComponentCount() : -1;
	{
		// "StereoInput 1" negotiates the stereo input bus with the full-width
		// output bus, which is what engages the upmix engine.
		double outputData[8][4] = {};
		runUpmixerFilter(upmixerLibrary, true, left, right, outputData);
		harness.expectTrue(test::nearlyEqual(outputData[0][0], left, 1.0e-9) && test::nearlyEqual(outputData[1][0], right, 1.0e-9),
			"StereoInput keeps the stereo source on the front channels");
		harness.expectNear(outputData[2][0], left + right, 1.0e-9,
			"StereoInput drives the center channel");
		harness.expectNear(outputData[3][0], 0.125 * (left + right), 1.0e-9,
			"StereoInput drives the LFE channel");
		harness.expectTrue(test::nearlyEqual(outputData[4][0], 0.25 * left, 1.0e-9) && test::nearlyEqual(outputData[5][0], 0.25 * right, 1.0e-9),
			"StereoInput drives the rear channels");
		harness.expectTrue(test::nearlyEqual(outputData[6][0], 0.5 * left, 1.0e-9) && test::nearlyEqual(outputData[7][0], 0.5 * right, 1.0e-9),
			"StereoInput drives the side channels");
	}
	if (upmixerComponentCount != nullptr)
		harness.expectEqual(upmixerComponentCount() - stereoInputInstancesBefore, 1,
			"StereoInput still uses exactly one plugin instance");
}

TEST_CASE(Vst3HostTests, testExplicitBusContract)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;
	const auto upmixerComponentCount = scenario.componentCount;

	const double left = 0.5;
	const double right = 0.25;
	{
		VST3BusContract contract;
		contract.input = VST3BusLayout::Stereo;
		contract.output = VST3BusLayout::Surround71;
		const int instancesBefore = upmixerComponentCount != nullptr ? upmixerComponentCount() : -1;
		VSTPluginFilter busFilter(upmixerLibrary, wstring(),
			std::unordered_map<wstring, float>(), contract);
		const std::vector<wstring> surroundChannels = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
		busFilter.initialize(48000.0f, 4, surroundChannels);
		double inputData[8][4] = {};
		double outputData[8][4] = {};
		double* inputs[8];
		double* outputs[8];
		for (int channel = 0; channel < 8; channel++)
		{
			inputs[channel] = inputData[channel];
			outputs[channel] = outputData[channel];
		}
		for (int sample = 0; sample < 4; sample++)
		{
			inputData[0][sample] = left;
			inputData[1][sample] = right;
		}
		busFilter.process(outputs, inputs, 4);
		harness.expectTrue(test::nearlyEqual(outputData[2][0], left + right, 1.0e-9)
			&& test::nearlyEqual(outputData[7][0], 0.5 * right, 1.0e-9),
			"VSTPlugin Stereo -> 7.1 processes the complete output bus");
		if (upmixerComponentCount != nullptr)
			harness.expectEqual(upmixerComponentCount() - instancesBefore, 1,
				"VSTPlugin explicit layout uses one instance for the complete output bus");
	}
}

TEST_CASE(Vst3HostTests, testRejectedBusContract)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;
	const auto upmixerProcessCount = scenario.processCount;

	{
		VST3BusContract rejectedContract;
		rejectedContract.input = VST3BusLayout::Stereo;
		rejectedContract.output = VST3BusLayout::Surround51;
		const int processCallsBefore = upmixerProcessCount != nullptr ? upmixerProcessCount() : -1;
		VSTPluginFilter rejectedFilter(upmixerLibrary, wstring(),
			std::unordered_map<wstring, float>(), rejectedContract);
		const std::vector<wstring> surroundChannels = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
		rejectedFilter.initialize(48000.0f, 4, surroundChannels);
		double inputData[8][4] = {};
		double outputData[8][4] = {};
		double* inputs[8];
		double* outputs[8];
		for (int channel = 0; channel < 8; channel++)
		{
			inputs[channel] = inputData[channel];
			outputs[channel] = outputData[channel];
			for (int sample = 0; sample < 4; sample++)
				inputData[channel][sample] = 0.1 * (channel + 1) + 0.01 * sample;
		}
		rejectedFilter.process(outputs, inputs, 4);
		bool passedThrough = true;
		for (int channel = 0; channel < 8; channel++)
		{
			for (int sample = 0; sample < 4; sample++)
				passedThrough = passedThrough && test::nearlyEqual(outputData[channel][sample], inputData[channel][sample], 1.0e-9);
		}
		harness.expectTrue(passedThrough,
			"rejected explicit VSTPlugin contract passes every device channel through");
		if (upmixerProcessCount != nullptr)
			harness.expectEqual(upmixerProcessCount() - processCallsBefore, 0,
				"rejected explicit VSTPlugin contract never calls plugin processing");
	}
}

TEST_CASE(Vst3HostTests, testAutomaticBusContract)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;

	const double left = 0.5;
	const double right = 0.25;
	{
		double legacyOutput[8][4] = {};
		runUpmixerFilter(upmixerLibrary, false, left, right, legacyOutput);
		VST3BusContract automaticContract;
		VSTPluginFilter automaticFilter(upmixerLibrary, wstring(),
			std::unordered_map<wstring, float>(), automaticContract);
		const std::vector<wstring> surroundChannels = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
		automaticFilter.initialize(48000.0f, 4, surroundChannels);
		double inputData[8][4] = {};
		double automaticOutput[8][4] = {};
		double* inputs[8];
		double* outputs[8];
		for (int channel = 0; channel < 8; channel++)
		{
			inputs[channel] = inputData[channel];
			outputs[channel] = automaticOutput[channel];
		}
		for (int sample = 0; sample < 4; sample++)
		{
			inputData[0][sample] = left;
			inputData[1][sample] = right;
		}
		automaticFilter.process(outputs, inputs, 4);
		bool sameAsLegacy = true;
		for (int channel = 0; channel < 8; channel++)
		{
			for (int sample = 0; sample < 4; sample++)
				sameAsLegacy = sameAsLegacy && test::nearlyEqual(automaticOutput[channel][sample], legacyOutput[channel][sample], 1.0e-9);
		}
		harness.expectTrue(sameAsLegacy, "VSTPlugin Auto -> Auto matches automatic negotiation");
	}
}

TEST_CASE(Vst3HostTests, testStereoDeviceUpmixer)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;

	{
		// Stereo devices must keep working through the same plugin: the
		// negotiation settles on the stereo layout the plugin also supports.
		VSTPluginFilter stereoFilter(upmixerLibrary, wstring(), std::unordered_map<wstring, float>());
		std::vector<wstring> stereoChannels = {L"L", L"R"};
		stereoFilter.initialize(48000.0f, 4, stereoChannels);
		double stereoIn[2][4] = {{0.5, -0.5, 0.25, -0.25}, {1.0, -1.0, 0.75, -0.75}};
		double stereoOut[2][4] = {};
		double* stereoInputs[2];
		double* stereoOutputs[2];
		for (int channel = 0; channel < 2; ++channel)
		{
			stereoInputs[channel] = stereoIn[channel];
			stereoOutputs[channel] = stereoOut[channel];
		}
		stereoFilter.process(stereoOutputs, stereoInputs, 4);
		harness.expectTrue(test::nearlyEqual(stereoOut[0][1], -0.5, 1.0e-9) && test::nearlyEqual(stereoOut[1][2], 0.75, 1.0e-9),
			"a stereo device still negotiates the upmixer's stereo layout");
	}
}

TEST_CASE(Vst3HostTests, testChannelFill)
{
	const auto scenario = loadUpmixerScenario();
	const auto& upmixerLibrary = scenario.library;
	const auto upmixerProcessCount = scenario.processCount;

	{
		// Per-slot channel fill on an explicit contract. Each config channel
		// carries its own DC level, so which channel reached which negotiated
		// slot - and which channel the plugin wrote - is visible in the output.
		const std::vector<wstring> surroundChannels = {L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR"};
		const double fillLevels[8] = {0.5, 0.25, 0.125, 0.0625, -0.5, -0.25, -0.125, -0.0625};
		const auto runChannelFill = [&upmixerLibrary, &surroundChannels, &fillLevels](
			VST3BusLayout inputLayout, VST3BusLayout outputLayout,
			const std::vector<wstring>& fillInput, const std::vector<wstring>& fillOutput,
			double (&firstSample)[8])
		{
			VST3BusContract contract;
			contract.input = inputLayout;
			contract.output = outputLayout;
			VSTPluginFilter filter(upmixerLibrary, wstring(), std::unordered_map<wstring, float>(),
				contract, fillInput, fillOutput);
			filter.initialize(48000.0f, 4, surroundChannels);

			double inputData[8][4] = {};
			double outputData[8][4] = {};
			double* inputs[8];
			double* outputs[8];
			for (int channel = 0; channel < 8; channel++)
			{
				inputs[channel] = inputData[channel];
				outputs[channel] = outputData[channel];
				for (int sample = 0; sample < 4; sample++)
					inputData[channel][sample] = fillLevels[channel];
			}
			filter.process(outputs, inputs, 4);
			for (int channel = 0; channel < 8; channel++)
				firstSample[channel] = outputData[channel][0];
		};

		double mappedInput[8] = {};
		runChannelFill(VST3BusLayout::Stereo, VST3BusLayout::Surround71,
			{L"RL", L"RR"}, std::vector<wstring>(), mappedInput);
		harness.expectTrue(test::nearlyEqual(mappedInput[0], fillLevels[4], 1.0e-9) && test::nearlyEqual(mappedInput[1], fillLevels[5], 1.0e-9),
			"InputChannels feeds the named config channels into the negotiated input slots");
		harness.expectTrue(test::nearlyEqual(mappedInput[2], fillLevels[4] + fillLevels[5], 1.0e-9)
			&& test::nearlyEqual(mappedInput[6], 0.5 * fillLevels[4], 1.0e-9),
			"a filled input bus drives every channel of the negotiated output bus");

		double silentSlotInput[8] = {};
		runChannelFill(VST3BusLayout::Stereo, VST3BusLayout::Surround71,
			{L"L", L"-"}, std::vector<wstring>(), silentSlotInput);
		harness.expectTrue(test::nearlyEqual(silentSlotInput[1], 0.0, 1.0e-9) && test::nearlyEqual(silentSlotInput[5], 0.0, 1.0e-9),
			"a dash input slot stays silent");
		harness.expectNear(silentSlotInput[2], fillLevels[0], 1.0e-9,
			"the filled input slots beside a dash keep their config channel");

		double mappedOutput[8] = {};
		runChannelFill(VST3BusLayout::Surround71, VST3BusLayout::Stereo,
			std::vector<wstring>(), {L"SL", L"SR"}, mappedOutput);
		harness.expectTrue(test::nearlyEqual(mappedOutput[6], fillLevels[0], 1.0e-9) && test::nearlyEqual(mappedOutput[7], fillLevels[1], 1.0e-9),
			"OutputChannels writes the negotiated output slots to the named config channels");
		bool untargetedPassedThrough = true;
		for (int channel = 0; channel < 6; channel++)
			untargetedPassedThrough = untargetedPassedThrough && test::nearlyEqual(mappedOutput[channel], fillLevels[channel], 1.0e-9);
		harness.expectTrue(untargetedPassedThrough,
			"config channels no output slot targets pass through instead of being zero-filled");

		double discardedOutput[8] = {};
		runChannelFill(VST3BusLayout::Stereo, VST3BusLayout::Stereo,
			{L"C", L"LFE"}, {L"L", L"-"}, discardedOutput);
		harness.expectNear(discardedOutput[0], fillLevels[2], 1.0e-9,
			"a filled input slot reaches the channel its output slot names");
		harness.expectNear(discardedOutput[1], fillLevels[1], 1.0e-9,
			"a dash output slot is discarded instead of reaching a config channel");
		harness.expectNear(discardedOutput[3], fillLevels[3], 1.0e-9,
			"a config channel read by an input slot still passes through");

		const int fillProcessCallsBefore = upmixerProcessCount != nullptr ? upmixerProcessCount() : -1;
		double unresolvedFill[8] = {};
		runChannelFill(VST3BusLayout::Stereo, VST3BusLayout::Surround71,
			{L"L", L"Nonexistent"}, std::vector<wstring>(), unresolvedFill);
		bool unresolvedPassedThrough = true;
		for (int channel = 0; channel < 8; channel++)
			unresolvedPassedThrough = unresolvedPassedThrough && test::nearlyEqual(unresolvedFill[channel], fillLevels[channel], 1.0e-9);
		harness.expectTrue(unresolvedPassedThrough,
			"a channel fill naming an absent channel passes every config channel through");
		if (upmixerProcessCount != nullptr)
			harness.expectEqual(upmixerProcessCount() - fillProcessCallsBefore, 0,
				"a channel fill naming an absent channel never calls plugin processing");

		// Copy-created virtual channels reach the fill by name: the engine
		// hands the filter the SELECTED channel list, which is where a
		// Channel: line places Copy targets, and resolution is the same
		// name lookup Copy uses - custom spellings included. Channel
		// restriction is the same fact from the other side: names outside
		// the handed-in list simply do not exist here.
		{
			const std::vector<wstring> virtualChannels = {L"V1", L"V2", L"L", L"R"};
			VST3BusContract virtualContract;
			virtualContract.input = VST3BusLayout::Stereo;
			virtualContract.output = VST3BusLayout::Stereo;
			VSTPluginFilter virtualFill(upmixerLibrary, wstring(), std::unordered_map<wstring, float>(),
				virtualContract, {L"V2", L"V1"}, {L"V1", L"-"});
			virtualFill.initialize(48000.0f, 4, virtualChannels);
			double inputData[4][4] = {};
			double outputData[4][4] = {};
			double* inputs[4];
			double* outputs[4];
			for (int channel = 0; channel < 4; channel++)
			{
				inputs[channel] = inputData[channel];
				outputs[channel] = outputData[channel];
				for (int sample = 0; sample < 4; sample++)
					inputData[channel][sample] = fillLevels[channel];
			}
			virtualFill.process(outputs, inputs, 4);
			harness.expectNear(outputData[0][0], fillLevels[1], 1.0e-9,
				"a virtual channel name feeds a slot and receives the slot's output");
			harness.expectTrue(test::nearlyEqual(outputData[1][0], fillLevels[1], 1.0e-9)
				&& test::nearlyEqual(outputData[2][0], fillLevels[2], 1.0e-9) && test::nearlyEqual(outputData[3][0], fillLevels[3], 1.0e-9),
				"channels no output slot targets pass through, virtual ones included");
		}
	}
}

TEST_CASE(Vst3HostTests, testUnwrittenChannelLatency)
{
	const wstring directory = exeDirectory();
	{
		// Audit #348 A10, maintainer decision: the latency compensation delays
		// only the channels the plugin does not write, so they line up with the
		// ones it does. The LatencyUpmixer module reports 512 samples of latency
		// but adds none, and its Stereo -> Stereo bus copies L and R through. A
		// fill over L, R, C leaves C unwritten: C comes out 512 samples late,
		// L and R with no extra delay.
		const wstring latencyBundle = prepareBundle(directory,
			L"LatencyUpmixerBundle.vst3", L"TestVst3LatencyUpmixer.vst3");
		shared_ptr<VSTPluginLibrary> latencyLibrary = VSTPluginLibrary::getInstance(latencyBundle);
		harness.expectTrue(!latencyBundle.empty() && VSTPluginLibrary::load(latencyLibrary->getLibPath()).succeeded(),
			"latency-reporting VST3 module initializes");

		VST3BusContract latencyContract;
		latencyContract.input = VST3BusLayout::Stereo;
		latencyContract.output = VST3BusLayout::Stereo;
		VSTPluginFilter latencyFilter(latencyLibrary, wstring(), std::unordered_map<wstring, float>(),
			latencyContract, {L"L", L"-"}, std::vector<wstring>());
		latencyFilter.initialize(48000.0f, 1024, {L"L", L"R", L"C"});
		const std::vector<unsigned> latencyInput = {5, 100, 200};
		std::vector<unsigned> latencyOutput = latencyInput;
		latencyFilter.reportLatency(latencyInput, latencyOutput);
		harness.expectTrue(latencyOutput == std::vector<unsigned>({517, 517, 712}),
			"an explicit VST3 input fill ignores host channels and silent slots it does not feed");

		const auto latencyStreamMatches = [&latencyLibrary](unsigned blockSize)
		{
			constexpr unsigned maxFrameCount = 1024;
			constexpr unsigned streamLength = 2048;
			constexpr unsigned latency = 512;
			VST3BusContract contract;
			contract.input = VST3BusLayout::Stereo;
			contract.output = VST3BusLayout::Stereo;
			VSTPluginFilter filter(latencyLibrary, wstring(), std::unordered_map<wstring, float>(),
				contract, {L"L", L"R"}, std::vector<wstring>());
			filter.initialize(48000.0f, maxFrameCount, {L"L", L"R", L"C"});

			std::vector<std::vector<double>> inputData(3, std::vector<double>(streamLength, 0.0));
			std::vector<std::vector<double>> outputData(3, std::vector<double>(streamLength, -1.0));
			for (int channel = 0; channel < 3; channel++)
				inputData[channel][0] = 1.0;
			for (unsigned offset = 0; offset < streamLength; offset += blockSize)
			{
				double* inputs[3];
				double* outputs[3];
				for (int channel = 0; channel < 3; channel++)
				{
					inputs[channel] = inputData[channel].data() + offset;
					outputs[channel] = outputData[channel].data() + offset;
				}
				filter.process(outputs, inputs, blockSize);
			}

			for (unsigned sample = 0; sample < streamLength; sample++)
			{
				const double delayed = sample >= latency ? inputData[2][sample - latency] : 0.0;
				if (outputData[0][sample] != inputData[0][sample]
					|| outputData[1][sample] != inputData[1][sample]
					|| outputData[2][sample] != delayed)
					return false;
			}
			return true;
		};
		harness.expectTrue(latencyStreamMatches(128),
			"128-frame blocks: the unwritten channel is 512 samples late, the written ones are not delayed");
		harness.expectTrue(latencyStreamMatches(1024),
			"1024-frame blocks: the unwritten channel is 512 samples late, the written ones are not delayed");
	}
}

TEST_CASE(Vst3HostTests, testInconsistentBusMetadata)
{
	const wstring directory = exeDirectory();
	const wstring mismatchBundle = prepareBundle(directory,
		L"BusInfoMismatchBundle.vst3", L"TestVst3BusInfoMismatch.vst3");
	shared_ptr<VSTPluginLibrary> mismatchLibrary = VSTPluginLibrary::getInstance(mismatchBundle);
	harness.expectTrue(!mismatchBundle.empty() && VSTPluginLibrary::load(mismatchLibrary->getLibPath()).succeeded(),
		"bus-info mismatch VST3 module initializes");
	{
		auto mismatchProbeOwner = VSTPluginInstance::create(mismatchLibrary, VSTPluginInstance::ProcessLevel::Realtime);
		VSTPluginInstance& mismatchProbe = *mismatchProbeOwner;
		harness.expectTrue(mismatchProbe.initialize(), "bus-info mismatch component initializes");
		harness.expectFalse(negotiateLayouts(mismatchProbe, VST3BusLayout::Stereo,
			VST3BusLayout::Surround71, 8,
			vst3BusLayoutChannelNames(VST3BusLayout::Stereo),
			vst3BusLayoutChannelNames(VST3BusLayout::Surround71)),
			"VSTPlugin explicit layout rejects inconsistent accepted bus metadata");
	}
}

TEST_CASE(Vst3HostTests, testSurround41Negotiation)
{
	const auto scenario = loadSurroundScenario();
	const auto& surround41Library = scenario.library;
	const auto surround41AcceptedArrangement = scenario.acceptedArrangement;

	{
		auto surround41ProbeOwner = VSTPluginInstance::create(surround41Library, VSTPluginInstance::ProcessLevel::Realtime);
		VSTPluginInstance& surround41Probe = *surround41ProbeOwner;
		const std::vector<wstring> surround41Channels = {L"L", L"R", L"LFE", L"RL", L"RR"};
		harness.expectTrue(surround41Probe.initialize(), "Surround41 VST3 component initializes");
		harness.expectTrue(negotiateChannels(surround41Probe, 5, surround41Channels),
			"semantic 4.1 names negotiate a five-channel bus");
		harness.expectTrue(surround41AcceptedArrangement != nullptr
			&& surround41AcceptedArrangement()
				== static_cast<unsigned long long>(Steinberg::Vst::SpeakerArr::k41Music),
			"semantic 4.1 names negotiate k41Music instead of k50");

		harness.expectTrue(negotiateLayouts(surround41Probe, VST3BusLayout::Surround41,
			VST3BusLayout::Surround41, 5,
			vst3BusLayoutChannelNames(VST3BusLayout::Surround41),
			vst3BusLayoutChannelNames(VST3BusLayout::Surround41)),
			"explicit VSTPlugin 4.1 layout is supported");
		const std::optional<VST3BusLayout> accepted41 = surround41Probe.getNegotiatedVST3OutputLayout();
		harness.expectTrue(accepted41 && *accepted41 == VST3BusLayout::Surround41,
			"accepted 4.1 arrangement is not misreported as same-width 5.0");
		harness.expectTrue(surround41AcceptedArrangement != nullptr
			&& surround41AcceptedArrangement()
				== static_cast<unsigned long long>(Steinberg::Vst::SpeakerArr::k41Music),
			"explicit VSTPlugin 4.1 prefers k41Music over same-width 5.0");
	}
}

TEST_CASE(Vst3HostTests, testSurround50Negotiation)
{
	const auto scenario = loadSurroundScenario();
	const auto& surround41Library = scenario.library;
	const auto surround41AcceptedArrangement = scenario.acceptedArrangement;

	{
		auto surround50ProbeOwner = VSTPluginInstance::create(surround41Library, VSTPluginInstance::ProcessLevel::Realtime);
		VSTPluginInstance& surround50Probe = *surround50ProbeOwner;
		const std::vector<wstring> surround50Channels = {L"L", L"R", L"C", L"RL", L"RR"};
		harness.expectTrue(surround50Probe.initialize(), "Surround41 component reinitializes for 5.0");
		harness.expectTrue(negotiateChannels(surround50Probe, 5, surround50Channels),
			"semantic 5.0 names negotiate a five-channel bus");
		harness.expectTrue(surround41AcceptedArrangement != nullptr
			&& surround41AcceptedArrangement()
				== static_cast<unsigned long long>(Steinberg::Vst::SpeakerArr::k50),
			"semantic 5.0 names negotiate k50");
	}
}

TEST_CASE(Vst3HostTests, testSurround41ChannelMapping)
{
	const auto scenario = loadSurroundScenario();
	const auto& surround41Library = scenario.library;

	{
		const std::vector<wstring> surround41Channels = {L"L", L"R", L"LFE", L"RL", L"RR"};
		VSTPluginFilter surround41Filter(surround41Library, wstring(),
			std::unordered_map<wstring, float>());
		surround41Filter.initialize(48000.0f, 4, surround41Channels);

		double inputData[5][4] = {};
		double outputData[5][4] = {};
		double* inputs[5];
		double* outputs[5];
		for (int channel = 0; channel < 5; channel++)
		{
			inputs[channel] = inputData[channel];
			outputs[channel] = outputData[channel];
		}
		surround41Filter.process(outputs, inputs, 4);

		harness.expectNear(outputData[2][0], 0.5, 1.0e-9,
			"4.1 EAPO LFE receives the accepted arrangement's Lfe bus slot");
		harness.expectNear(outputData[3][0], 0.375, 1.0e-9,
			"4.1 EAPO RL receives the accepted arrangement's Ls bus slot");
		harness.expectNear(outputData[4][0], 0.4375, 1.0e-9,
			"4.1 EAPO RR receives the accepted arrangement's Rs bus slot");
	}
}

TEST_CASE(Vst3HostTests, testSurround41CineNegotiation)
{
	const wstring directory = exeDirectory();
	typedef unsigned long long (*Surround41ArrangementFunc)();
	const wstring surround41CineOnlyBundle = prepareBundle(directory,
		L"Surround41CineOnlyBundle.vst3", L"TestVst3Surround41CineOnly.vst3");
	shared_ptr<VSTPluginLibrary> surround41CineOnlyLibrary =
		VSTPluginLibrary::getInstance(surround41CineOnlyBundle);
	harness.expectTrue(!surround41CineOnlyBundle.empty()
		&& VSTPluginLibrary::load(surround41CineOnlyLibrary->getLibPath()).succeeded(),
		"4.1 Cine-only VST3 module initializes");
	HMODULE surround41CineOnlyModule = GetModuleHandleW(L"TestVst3Surround41CineOnly.vst3");
	Surround41ArrangementFunc surround41CineOnlyAcceptedArrangement = surround41CineOnlyModule != nullptr
		? reinterpret_cast<Surround41ArrangementFunc>(
			GetProcAddress(surround41CineOnlyModule, "GetSurround41AcceptedOutputArrangement")) : nullptr;
	harness.expectTrue(surround41CineOnlyAcceptedArrangement != nullptr,
		"4.1 Cine-only accepted-arrangement probe is exported");
	{
		auto cineOnlyProbeOwner = VSTPluginInstance::create(surround41CineOnlyLibrary, VSTPluginInstance::ProcessLevel::Realtime);
		VSTPluginInstance& cineOnlyProbe = *cineOnlyProbeOwner;
		harness.expectTrue(cineOnlyProbe.initialize(), "4.1 Cine-only component initializes");
		harness.expectTrue(negotiateLayouts(cineOnlyProbe, VST3BusLayout::Surround41,
			VST3BusLayout::Surround41, 5,
			vst3BusLayoutChannelNames(VST3BusLayout::Surround41),
			vst3BusLayoutChannelNames(VST3BusLayout::Surround41)),
			"explicit 4.1 tries the allowed same-layout Cine alternative");
		harness.expectTrue(surround41CineOnlyAcceptedArrangement != nullptr
			&& surround41CineOnlyAcceptedArrangement()
				== static_cast<unsigned long long>(Steinberg::Vst::SpeakerArr::k41Cine),
			"explicit 4.1 records the accepted k41Cine alternative");
	}
}

TEST_CASE(Vst3HostTests, testStereoFallbackMapping)
{
	auto library = loadScenarioLibrary();
	{
		// The default component remains stereo-only. The semantic k41Music
		// and count-based k50 proposals are rejected, after which each split
		// instance uses its reported stereo arrangement and identity fallback.
		const std::vector<wstring> surround41Channels = {L"L", L"R", L"LFE", L"RL", L"RR"};
		VSTPluginFilter stereoFallbackFilter(library, wstring(),
			std::unordered_map<wstring, float>());
		stereoFallbackFilter.initialize(48000.0f, 4, surround41Channels);

		double inputData[5][4] = {
			{0.1, 0.2, 0.3, 0.4},
			{0.5, 0.6, 0.7, 0.8},
			{0.9, 1.0, 1.1, 1.2},
			{1.3, 1.4, 1.5, 1.6},
			{1.7, 1.8, 1.9, 2.0}
		};
		double outputData[5][4] = {};
		double* inputs[5];
		double* outputs[5];
		for (int channel = 0; channel < 5; channel++)
		{
			inputs[channel] = inputData[channel];
			outputs[channel] = outputData[channel];
		}
		stereoFallbackFilter.process(outputs, inputs, 4);

		// The default factory rotates its component scenarios, so one split
		// instance is float-only and its channels round-trip double->float->
		// double. A float tolerance still proves what this test is about:
		// no channel ends up swapped, scaled or silenced by a stale mapping.
		bool fallbackPassedThrough = true;
		for (int channel = 0; channel < 5; channel++)
		{
			for (int sample = 0; sample < 4; sample++)
			{
				fallbackPassedThrough = fallbackPassedThrough
					&& std::fabs(outputData[channel][sample] - inputData[channel][sample]) <= 1.0e-6;
			}
		}
		harness.expectTrue(fallbackPassedThrough,
			"4.1 names fall back cleanly through the default stereo-only component");
	}
}

// Issue #519. A device that reports no channel mask (Benchmark passes 0, and a
// 16-channel virtual cable reported none either) names its channels 1..16, so
// a configuration makes L and R with Copy, which does not select what it
// creates. A fill naming them used to find nothing in the selection and
// disabled the plug-in. A fill now reaches every channel that exists at its
// line, device or virtual, selected or not. The selection the engine hands
// the plug-in still comes first, so a line that already worked keeps its exact
// output and latency, and numbers stay positions in that selection.
TEST_CASE(Vst3HostTests, testChannelFillReachesUnselectedChannels)
{
	const wstring latencyBundle = prepareBundle(exeDirectory(),
		L"LatencyUpmixerBundle.vst3", L"TestVst3LatencyUpmixer.vst3");
	harness.require(!latencyBundle.empty(), "latency-reporting VST3 bundle is prepared");
	const std::string library = "VSTPlugin: Library \"" + wintext::toNarrowString(latencyBundle, CP_UTF8) + "\"";

	// The LatencyUpmixer module copies its Stereo input to its Stereo output
	// unchanged and reports 512 samples of latency, so the output shows which
	// channels the plug-in read and wrote, and which ones the engine delayed.
	constexpr unsigned channels = 16;
	constexpr unsigned frames = 1024;
	constexpr unsigned latency = 512;
	test::TestDirectory files(L"Vst3FillSelection");

	struct Rendered
	{
		std::vector<double> output;
		std::vector<unsigned> latency;
		std::string log;
		double at(unsigned channel, unsigned frame) const
		{
			return output[static_cast<size_t>(frame) * channels + channel];
		}
	};
	const auto render = [&files](const wstring& name, const std::string& body) {
		const wstring configPath = files.trackFile(name + L".txt");
		const wstring logPath = files.trackFile(name + L".log");
		{
			std::ofstream config(configPath, std::ios::binary);
			config << body;
		}
		DeleteFileW(logPath.c_str());
		Logging::useFile(logPath, false, true, false);
		Rendered rendered;
		{
			FilterEngine engine;
			EngineSetup setup;
			setup.sampleRate = 48000.0f;
			setup.inputChannelCount = channels;
			setup.realChannelCount = channels;
			setup.outputChannelCount = channels;
			setup.channelMask = 0;
			setup.maxFrameCount = frames;
			setup.customPath = configPath;
			setup.deviceName = L"Vst3HostTests";
			setup.connectionName = L"File";
			engine.initialize(setup);

			// Impulses on channels 1, 2 and 4 at the first frame.
			std::vector<double> input(static_cast<size_t>(2) * frames * channels, 0.0);
			input[0] = 0.5;
			input[1] = 0.25;
			input[3] = 0.125;
			rendered.output.assign(input.size(), 0.0);
			for (unsigned block = 0; block < 2; ++block)
			{
				const size_t offset = static_cast<size_t>(block) * frames * channels;
				engine.process(rendered.output.data() + offset, input.data() + offset, frames);
			}
			rendered.latency = engine.getRealLatencyByChannel();
		}
		Logging::set(stdout, true, true, false);
		std::ifstream log(logPath, std::ios::binary);
		rendered.log.assign(std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>());
		return rendered;
	};

	// The issue's shape: no Channel: line, so the selection at the plug-in is
	// the sixteen device channels plus the two NewChannel names; L and R exist
	// but are not selected.
	const std::string fill = library + " Input Stereo InputChannels L,R Output Stereo OutputChannels VA,VB\n";
	const Rendered unselected = render(L"unselected",
		"Copy: L=1 R=2\nNewChannel: VA VB\n" + fill + "Copy: 1=VA 2=VB 3=L\n");
	harness.expectTrue(unselected.log.find("passing audio through") == std::string::npos,
		"a fill naming Copy-created channels that are not selected keeps the plug-in running");
	harness.expectTrue(test::nearlyEqual(unselected.at(0, 0), 0.5, 1.0e-9)
		&& test::nearlyEqual(unselected.at(1, 0), 0.25, 1.0e-9),
		"the plug-in reads the unselected L and R and writes the selected virtual channels");
	harness.expectTrue(test::nearlyEqual(unselected.at(2, latency), 0.5, 1.0e-9)
		&& test::nearlyEqual(unselected.at(2, 0), 0.0, 1.0e-9),
		"a channel the fill only reads passes through delayed by the plug-in latency, like a selected one");
	harness.expectTrue(unselected.latency.size() == channels
		&& unselected.latency[0] == latency && unselected.latency[1] == latency && unselected.latency[2] == latency,
		"the written channels and the delayed reader report the plug-in latency");

	// The workaround the issue found. It worked before this change and must
	// not change at all: channel 4 is outside the selection, so it is neither
	// processed nor delayed.
	const Rendered selected = render(L"selected",
		"Copy: L=1 R=2\nChannel: L R\nNewChannel: VA VB\n" + fill + "Copy: 1=VA 2=VB 3=L\n");
	harness.expectTrue(test::nearlyEqual(selected.at(0, 0), 0.5, 1.0e-9)
		&& test::nearlyEqual(selected.at(1, 0), 0.25, 1.0e-9)
		&& test::nearlyEqual(selected.at(2, latency), 0.5, 1.0e-9),
		"the Channel: L R workaround still runs the plug-in");
	harness.expectTrue(test::nearlyEqual(selected.at(3, 0), 0.125, 1.0e-9) && selected.latency.size() == channels
		&& selected.latency[3] == 0,
		"a channel outside a Channel: selection stays undelayed");

	// A name that no line created still disables the plug-in, and the log says
	// which name.
	const Rendered absent = render(L"absent",
		"Copy: L=1 R=2\nNewChannel: VA VB\n" + library
		+ " Input Stereo InputChannels L,NOSUCH Output Stereo OutputChannels VA,VB\nCopy: 1=VA 2=VB\n");
	harness.expectTrue(test::nearlyEqual(absent.at(0, 0), 0.0, 1.0e-9),
		"a fill naming a channel that does not exist disables the plug-in");
	harness.expectTrue(absent.log.find("NOSUCH") != std::string::npos
		&& absent.log.find("passing audio through") != std::string::npos,
		"the refusal names the channel it could not find");

	// A side without a fill writes the first channels of the selection, never
	// an appended one. With every device channel selected, the plug-in's
	// output lands on channels 1 and 2; the unselected L it read passes through
	// delayed, like channel 4.
	const Rendered oneSided = render(L"onesided",
		"Copy: L=1 R=2\n" + library + " Input Stereo InputChannels L,R Output Stereo\nCopy: 3=L\n");
	harness.expectTrue(oneSided.log.find("passing audio through") == std::string::npos
		&& test::nearlyEqual(oneSided.at(0, 0), 0.5, 1.0e-9) && test::nearlyEqual(oneSided.at(1, 0), 0.25, 1.0e-9),
		"an input-only fill naming unselected channels writes the first selected channels");
	harness.expectTrue(test::nearlyEqual(oneSided.at(2, latency), 0.5, 1.0e-9)
		&& test::nearlyEqual(oneSided.at(3, latency), 0.125, 1.0e-9) && test::nearlyEqual(oneSided.at(3, 0), 0.0, 1.0e-9),
		"the read channel and the other selected channels pass through delayed");

	// With a selection narrower than the output bus, the slot beyond it is
	// discarded into padding sized from the selection, and the channels
	// outside the selection stay untouched.
	const Rendered narrow = render(L"narrow",
		"Copy: L=1 R=2\nChannel: 1\n" + library + " Input Stereo InputChannels L,R Output Stereo\nCopy: 3=L\n");
	harness.expectTrue(narrow.log.find("passing audio through") == std::string::npos
		&& test::nearlyEqual(narrow.at(0, 0), 0.5, 1.0e-9),
		"an input-only fill over a one-channel selection writes that channel");
	harness.expectTrue(test::nearlyEqual(narrow.at(1, 0), 0.25, 1.0e-9) && test::nearlyEqual(narrow.at(3, 0), 0.125, 1.0e-9)
		&& test::nearlyEqual(narrow.at(2, latency), 0.5, 1.0e-9),
		"the second output slot is discarded and unselected device channels stay undelayed");

	// Numbers stay positions in the selection: with only L selected, "2" is out
	// of range even though R is appended for the name beside it.
	const Rendered numbered = render(L"numbered",
		"Copy: L=1 R=2\nChannel: L\n" + library
		+ " Input Stereo InputChannels R,2 Output Stereo OutputChannels L,-\nCopy: 1=L\n");
	harness.expectTrue(test::nearlyEqual(numbered.at(0, 0), 0.5, 1.0e-9)
		&& numbered.log.find("passing audio through") != std::string::npos,
		"a channel number beyond the selection still disables the plug-in");
	files.removeAll();
}
