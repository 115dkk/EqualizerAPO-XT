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
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include "helpers/VSTPluginLibrary.h"
#include "helpers/VSTPluginInstance.h"
#include "filters/VSTPluginFilter.h"
// After VSTPluginInstance.h: the VST3 SDK defines a VST_VERSION macro that
// would otherwise break the enum of the same name in the VST2 aeffectx.h.
#include "pluginterfaces/vst/vstspeaker.h"
#include "Tests/TestHarness.h"
#include "Tests/TestVst3Plugin/TestVst3Protocol.h"

using std::shared_ptr;
using std::wstring;

namespace
{
test::Harness harness("Vst3HostTests");

using testvst3::PluginState;

wstring encodeState(const PluginState& state)
{
	DWORD length = 0;
	CryptBinaryToStringW(reinterpret_cast<const BYTE*>(&state), sizeof(state),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &length);
	if (length == 0)
		return wstring();
	std::vector<wchar_t> value(length);
	return CryptBinaryToStringW(reinterpret_cast<const BYTE*>(&state), sizeof(state),
		CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, value.data(), &length) == TRUE ? wstring(value.data()) : wstring();
}

bool closeEnough(double actual, double expected)
{
	return std::fabs(actual - expected) <= 1.0e-9;
}

wstring exeDirectory()
{
	wchar_t path[MAX_PATH] = {};
	DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return wstring();
	wstring full(path, length);
	size_t slash = full.find_last_of(L"\\/");
	return slash == wstring::npos ? wstring() : full.substr(0, slash);
}

bool ensureDirectory(const wstring& path)
{
	return CreateDirectoryW(path.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
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

wstring prepareBundle(const wstring& directory, const wchar_t* bundleName = L"TestVst3Bundle.vst3",
	const wchar_t* moduleName = L"TestVst3Plugin.vst3")
{
	const wstring source = directory + L"\\TestVst3PluginModule.vst3";
	if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES)
		return wstring();

	const wstring bundle = directory + L"\\" + bundleName;
	const wstring contents = bundle + L"\\Contents";
	const wstring module = bundleModulePath(bundle, moduleName);
	const size_t platformSlash = module.find_last_of(L"\\/");
	const wstring platform = module.substr(0, platformSlash);
	if (!ensureDirectory(bundle) || !ensureDirectory(contents) || !ensureDirectory(platform))
		return wstring();

	DeleteFileW((module + L".exit").c_str());
	if (CopyFileW(source.c_str(), module.c_str(), FALSE) == FALSE)
		return wstring();
	return bundle;
}
}

void runVst3HostTests()
{
	const wstring directory = exeDirectory();
	const wstring bundle = directory.empty() ? wstring() : prepareBundle(directory);
	if (bundle.empty())
	{
		harness.expectFalse(bundle.empty(), "companion VST3 module is available");
		harness.report();
		return;
	}

	shared_ptr<VSTPluginLibrary> library = VSTPluginLibrary::getInstance(bundle);
	harness.require(library != nullptr, "bundle resolves to a library");
	harness.expectTrue(library->isVST3(), "bundle is recognized as VST3");
	harness.expectTrue(library->initialize() >= 0, "Windows VST3 module lifecycle initializes before factory access");
	harness.expectTrue(library->getFactory() != nullptr, "VST3 factory is available after module initialization");

	VSTPluginInstance instance(library, 2);
	harness.expectTrue(instance.initialize(), "VST3 component initializes with mandatory host services");
	harness.expectEqual(instance.numInputs(), 2, "VST3 component reports stereo input");
	harness.expectEqual(instance.numOutputs(), 2, "VST3 component reports stereo output");
	harness.expectTrue(instance.canDoubleReplacing(), "VST3 component advertises double processing");

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
	instance.processDoubleReplacing(doubleInputs, doubleOutputs, 4);
	harness.expectTrue(closeEnough(doubleOutLeft[2], 0.375) && closeEnough(doubleOutRight[0], -0.5),
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
	instance.processDoubleReplacing(doubleInputs, doubleOutputs, 4);
	harness.expectTrue(closeEnough(doubleOutLeft[2], 0.5625) && closeEnough(doubleOutRight[0], -0.75),
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
	SendMessageW(pluginWindow, WM_APP + 78, 0, 0);
	wstring componentAndControllerState;
	std::unordered_map<wstring, float> controllerStateParameters;
	instance.readFromEffect(componentAndControllerState, controllerStateParameters);
	harness.expectTrue(!componentAndControllerState.empty() && componentAndControllerState != componentOnlyState,
		"VST3 controller-private state is saved with component state");
	instance.stopEditing();
	harness.expectFalse(IsWindow(pluginWindow) != FALSE, "VST3 editor child is destroyed on close");
	DestroyWindow(parent);

	VSTPluginInstance singleComponentInstance(library, 2);
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
	harness.expectTrue(!controllerBundle.empty() && controllerLibrary->initialize() >= 0,
		"separate VST3 module initializes for controller-state restore");
	VSTPluginInstance controllerStateInstance(controllerLibrary, 2);
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
	harness.expectTrue(!controllerOnlyRestoreBundle.empty() && controllerOnlyRestoreLibrary->initialize() >= 0,
		"separate VST3 module initializes for controller-only state restore");
	VSTPluginInstance controllerOnlyRestoreInstance(controllerOnlyRestoreLibrary, 2);
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

	VSTPluginInstance floatOnlyInstance(library, 2);
	harness.expectTrue(floatOnlyInstance.initialize() && !floatOnlyInstance.canDoubleReplacing(),
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
	floatOnlyInstance.processReplacing(floatInputs, floatOutputs, 4);
	harness.expectTrue(closeEnough(floatOutLeft[2], 0.375) && closeEnough(floatOutRight[0], -0.5),
		"VST3 float processing applies restored state");
	floatOnlyInstance.stopProcessing();

	const wstring rejectedComponentBundle = prepareBundle(directory,
		L"RejectComponentBundle.vst3", L"RejectComponent.vst3");
	shared_ptr<VSTPluginLibrary> rejectedComponentLibrary = VSTPluginLibrary::getInstance(rejectedComponentBundle);
	HANDLE invalidTerminate = CreateEventW(nullptr, TRUE, FALSE, testvst3::invalidTerminateEvent);
	harness.expectTrue(!rejectedComponentBundle.empty() && rejectedComponentLibrary->initialize() >= 0,
		"VST3 module remains loadable when its component rejects initialization");
	{
		VSTPluginInstance rejectedComponentInstance(rejectedComponentLibrary, 2);
		harness.expectFalse(rejectedComponentInstance.initialize(),
			"VST3 component initialization failures are reported");
	}
	harness.expectTrue(invalidTerminate != nullptr
		&& WaitForSingleObject(invalidTerminate, 0) == WAIT_TIMEOUT,
		"a VST3 component is terminated only after successful initialization");
	if (invalidTerminate != nullptr)
		CloseHandle(invalidTerminate);

	const wstring rejectedBundle = prepareBundle(directory, L"RejectInitBundle.vst3", L"RejectInit.vst3");
	shared_ptr<VSTPluginLibrary> rejectedLibrary = VSTPluginLibrary::getInstance(rejectedBundle);
	harness.expectTrue(!rejectedBundle.empty() && rejectedLibrary->initialize() < 0,
		"VST3 module initialization failures are reported");
	harness.expectTrue(rejectedLibrary->initialize() < 0,
		"failed VST3 module initialization remains failed when retried");

	const wstring exitBundle = prepareBundle(directory, L"ExitInitBundle.vst3", L"ExitInit.vst3");
	const wstring exitMarker = bundleModulePath(exitBundle, L"ExitInit.vst3") + L".exit";
	{
		shared_ptr<VSTPluginLibrary> exitLibrary = VSTPluginLibrary::getInstance(exitBundle);
		harness.expectTrue(!exitBundle.empty() && exitLibrary->initialize() >= 0,
			"successful VST3 module is ready for lifecycle teardown");
	}
	harness.expectTrue(GetFileAttributesW(exitMarker.c_str()) != INVALID_FILE_ATTRIBUTES,
		"VST3 module ExitDll runs before the successful library unloads");

	// Multichannel bus negotiation. The companion module in "Upmixer.vst3"
	// mode reproduces the OpenSpatial Upmixer contract measured in PR #213:
	// it accepts stereo, symmetric 7.1, and a stereo input bus feeding a 7.1
	// output bus, but its upmix engine only runs on that last, DAW-style
	// asymmetric layout. On a symmetric 7.1 layout it passes the front pair
	// through and leaves the other six channels silent - the "only the front
	// speakers play" failure reported by users.
	const wstring upmixerBundle = prepareBundle(directory, L"UpmixerBundle.vst3", L"TestVst3Upmixer.vst3");
	shared_ptr<VSTPluginLibrary> upmixerLibrary = VSTPluginLibrary::getInstance(upmixerBundle);
	harness.expectTrue(!upmixerBundle.empty() && upmixerLibrary->initialize() >= 0,
		"upmixer VST3 module initializes");

	typedef int (*UpmixerCountFunc)();
	HMODULE upmixerModule = GetModuleHandleW(L"TestVst3Upmixer.vst3");
	UpmixerCountFunc upmixerComponentCount = upmixerModule != nullptr
		? reinterpret_cast<UpmixerCountFunc>(GetProcAddress(upmixerModule, "GetUpmixerComponentCount")) : nullptr;
	harness.expectTrue(upmixerComponentCount != nullptr, "upmixer instance counter is exported");

	{
		VSTPluginInstance upmixerProbe(upmixerLibrary, 2);
		harness.expectTrue(upmixerProbe.initialize(), "upmixer VST3 component initializes");
		harness.expectTrue(upmixerProbe.negotiateChannelCount(8), "upmixer negotiates up to a 7.1 bus");
		harness.expectEqual(upmixerProbe.numInputs(), 8, "negotiated upmixer input bus spans 8 channels");
		harness.expectEqual(upmixerProbe.numOutputs(), 8, "negotiated upmixer output bus spans 8 channels");
		harness.expectTrue(upmixerProbe.negotiateBusChannelCounts(2, 8),
			"upmixer accepts the stereo-input/7.1-output layout");
		harness.expectEqual(upmixerProbe.numInputs(), 2, "asymmetric layout keeps the input bus stereo");
		harness.expectEqual(upmixerProbe.numOutputs(), 8, "asymmetric layout keeps the output bus at 8 channels");
	}

	const auto runUpmixerFilter = [&upmixerLibrary](bool stereoInput, double left, double right, double (&outputData)[8][4])
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
	};

	const double left = 0.5;
	const double right = 0.25;

	const int upmixerInstancesBefore = upmixerComponentCount != nullptr ? upmixerComponentCount() : -1;
	{
		// Without StereoInput the host keeps symmetric buses: a single
		// full-width instance, front pair passed through, engine off. This
		// documents the measured real-plugin behavior rather than a host
		// defect - the layout choice must stay an explicit opt-in.
		double outputData[8][4] = {};
		runUpmixerFilter(false, left, right, outputData);
		harness.expectTrue(closeEnough(outputData[0][0], left) && closeEnough(outputData[1][0], right),
			"symmetric 7.1 layout passes the front pair through");
		harness.expectTrue(closeEnough(outputData[2][0], 0.0) && closeEnough(outputData[4][0], 0.0),
			"symmetric 7.1 layout leaves the upmix engine disengaged");
	}
	if (upmixerComponentCount != nullptr)
		harness.expectEqual(upmixerComponentCount() - upmixerInstancesBefore, 1,
			"a 7.1 device is served by exactly one full-width upmixer instance");

	const int stereoInputInstancesBefore = upmixerComponentCount != nullptr ? upmixerComponentCount() : -1;
	{
		// "StereoInput 1" negotiates the stereo input bus with the full-width
		// output bus, which is what engages the upmix engine.
		double outputData[8][4] = {};
		runUpmixerFilter(true, left, right, outputData);
		harness.expectTrue(closeEnough(outputData[0][0], left) && closeEnough(outputData[1][0], right),
			"StereoInput keeps the stereo source on the front channels");
		harness.expectTrue(closeEnough(outputData[2][0], left + right),
			"StereoInput drives the center channel");
		harness.expectTrue(closeEnough(outputData[3][0], 0.125 * (left + right)),
			"StereoInput drives the LFE channel");
		harness.expectTrue(closeEnough(outputData[4][0], 0.25 * left) && closeEnough(outputData[5][0], 0.25 * right),
			"StereoInput drives the rear channels");
		harness.expectTrue(closeEnough(outputData[6][0], 0.5 * left) && closeEnough(outputData[7][0], 0.5 * right),
			"StereoInput drives the side channels");
	}
	if (upmixerComponentCount != nullptr)
		harness.expectEqual(upmixerComponentCount() - stereoInputInstancesBefore, 1,
			"StereoInput still uses exactly one plugin instance");

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
		harness.expectTrue(closeEnough(stereoOut[0][1], -0.5) && closeEnough(stereoOut[1][2], 0.75),
			"a stereo device still negotiates the upmixer's stereo layout");
	}

	// Semantic 4.1/5.0 negotiation and accepted-arrangement channel mapping.
	const wstring surround41Bundle = prepareBundle(directory,
		L"Surround41Bundle.vst3", L"TestVst3Surround41.vst3");
	shared_ptr<VSTPluginLibrary> surround41Library = VSTPluginLibrary::getInstance(surround41Bundle);
	harness.expectTrue(!surround41Bundle.empty() && surround41Library->initialize() >= 0,
		"Surround41 VST3 module initializes");

	typedef unsigned long long (*Surround41ArrangementFunc)();
	HMODULE surround41Module = GetModuleHandleW(L"TestVst3Surround41.vst3");
	Surround41ArrangementFunc surround41AcceptedArrangement = surround41Module != nullptr
		? reinterpret_cast<Surround41ArrangementFunc>(
			GetProcAddress(surround41Module, "GetSurround41AcceptedOutputArrangement")) : nullptr;
	harness.expectTrue(surround41AcceptedArrangement != nullptr,
		"Surround41 accepted-arrangement probe is exported");

	{
		VSTPluginInstance surround41Probe(surround41Library, 2);
		const std::vector<wstring> surround41Channels = {L"L", L"R", L"LFE", L"RL", L"RR"};
		harness.expectTrue(surround41Probe.initialize(), "Surround41 VST3 component initializes");
		surround41Probe.setChannelNameHints(surround41Channels);
		harness.expectTrue(surround41Probe.negotiateChannelCount(5),
			"semantic 4.1 names negotiate a five-channel bus");
		harness.expectTrue(surround41AcceptedArrangement != nullptr
			&& surround41AcceptedArrangement()
				== static_cast<unsigned long long>(Steinberg::Vst::SpeakerArr::k41Music),
			"semantic 4.1 names negotiate k41Music instead of k50");
	}

	{
		VSTPluginInstance surround50Probe(surround41Library, 2);
		const std::vector<wstring> surround50Channels = {L"L", L"R", L"C", L"RL", L"RR"};
		harness.expectTrue(surround50Probe.initialize(), "Surround41 component reinitializes for 5.0");
		surround50Probe.setChannelNameHints(surround50Channels);
		harness.expectTrue(surround50Probe.negotiateChannelCount(5),
			"semantic 5.0 names negotiate a five-channel bus");
		harness.expectTrue(surround41AcceptedArrangement != nullptr
			&& surround41AcceptedArrangement()
				== static_cast<unsigned long long>(Steinberg::Vst::SpeakerArr::k50),
			"semantic 5.0 names negotiate k50");
	}

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

		harness.expectTrue(closeEnough(outputData[2][0], 0.5),
			"4.1 EAPO LFE receives the accepted arrangement's Lfe bus slot");
		harness.expectTrue(closeEnough(outputData[3][0], 0.375),
			"4.1 EAPO RL receives the accepted arrangement's Ls bus slot");
		harness.expectTrue(closeEnough(outputData[4][0], 0.4375),
			"4.1 EAPO RR receives the accepted arrangement's Rs bus slot");
	}

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

	harness.report();
}
