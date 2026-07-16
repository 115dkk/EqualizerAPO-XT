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
	harness.expectTrue(library != nullptr, "bundle resolves to a library");
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
	std::thread processingStarter([&instance, flushEntered]() {
		WaitForSingleObject(flushEntered, 5000);
		instance.startProcessing();
	});
	std::thread flushReleaser([flushEntered, flushContinue]() {
		if (WaitForSingleObject(flushEntered, 5000) == WAIT_OBJECT_0)
		{
			Sleep(100);
			SetEvent(flushContinue);
		}
	});
	SendMessageW(pluginWindow, WM_APP + 77, 0, 0);
	processingStarter.join();
	flushReleaser.join();
	harness.expectTrue(flushEntered != nullptr && flushContinue != nullptr && concurrentProcessing != nullptr
		&& WaitForSingleObject(concurrentProcessing, 0) == WAIT_TIMEOUT,
		"stopped-editor flush is serialized with audio processing startup");
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

	harness.report();
}
