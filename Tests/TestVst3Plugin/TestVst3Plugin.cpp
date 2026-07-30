/*
    A deterministic VST3 module used by the host integration tests. It requires
    the Windows module lifecycle entry point before exposing its factory, asks
    the host for the mandatory IMessage service during component initialization,
    processes stereo float/double audio, persists component state, and supplies
    a native HWND editor that requests a resize from its IPlugFrame.
*/

#define INIT_CLASS_IID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "TestVst3Protocol.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
const TUID componentCid = INLINE_UID(0x7A53DB11, 0x446B41D4, 0x9D7659A1, 0x19A23401);
const TUID controllerCid = INLINE_UID(0xC49B9B30, 0x72E44A1B, 0x935EAB31, 0x2EEFA402);
constexpr ParamID gainParamId = 100;

enum class ComponentScenario
{
	splitDouble,
	combinedDouble,
	splitFloat
};

bool moduleInitialized = false;
bool rejectComponentInitialize = false;
bool componentStateUnavailable = false;
bool upmixerMode = false;
bool surround41Mode = false;
std::atomic<int> upmixerComponentCount{0};
std::atomic<unsigned long long> surround41AcceptedOutputArrangement{
	static_cast<unsigned long long>(SpeakerArr::kStereo)
};
std::atomic<bool> zeroSampleFlushInProgress{false};
wchar_t loadedModulePath[MAX_PATH] = {};

bool iidIs(const TUID iid, const FUID& expected)
{
	return FUnknownPrivate::iidEqual(iid, expected);
}

void copyTuid(TUID destination, const TUID source)
{
	std::memcpy(destination, source, sizeof(TUID));
}

void copyString128(String128 destination, const wchar_t* source)
{
	wcsncpy_s(reinterpret_cast<wchar_t*>(destination), 128, source, _TRUNCATE);
}

class RefCounted
{
protected:
	virtual ~RefCounted() = default;

	uint32 retain() { return ++refCount; }
	uint32 drop()
	{
		uint32 remaining = --refCount;
		if (remaining == 0)
			delete this;
		return remaining;
	}

private:
	std::atomic<uint32> refCount{1};
};

using testvst3::PluginState;

constexpr uint32 controllerStateMagic = 0x33554954; // TUI3

#pragma pack(push, 1)
struct ControllerPrivateState
{
	uint32 magic = controllerStateMagic;
	double zoom = 1.0;
};
#pragma pack(pop)

bool readState(IBStream* stream, PluginState& state)
{
	if (stream == nullptr)
		return false;
	int32 bytesRead = 0;
	PluginState incoming;
	if (stream->read(&incoming, sizeof(incoming), &bytesRead) != kResultOk
		|| bytesRead != sizeof(incoming) || incoming.magic != testvst3::stateMagic)
		return false;
	state = incoming;
	return true;
}

template<class State>
tresult writeState(IBStream* stream, const State& state)
{
	if (stream == nullptr)
		return kInvalidArgument;
	int32 bytesWritten = 0;
	return stream->write(const_cast<State*>(&state), sizeof(state), &bytesWritten) == kResultOk
		&& bytesWritten == sizeof(state) ? kResultOk : kResultFalse;
}

bool readControllerState(IBStream* stream, ControllerPrivateState& state)
{
	if (stream == nullptr)
		return false;
	int32 bytesRead = 0;
	ControllerPrivateState incoming;
	if (stream->read(&incoming, sizeof(incoming), &bytesRead) != kResultOk
		|| bytesRead != sizeof(incoming) || incoming.magic != controllerStateMagic)
		return false;
	state = incoming;
	return true;
}

class TestView final : public IPlugView, public IPlugViewContentScaleSupport, private RefCounted
{
public:
	TestView(PluginState* state, IComponentHandler* handler, ControllerPrivateState* privateState, bool* privateStateDirty)
		: state(state), handler(handler), privateState(privateState), privateStateDirty(privateStateDirty)
	{
		if (handler != nullptr)
			handler->addRef();
	}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (obj == nullptr)
			return kInvalidArgument;
		if (iidIs(iid, FUnknown::iid) || iidIs(iid, IPlugView::iid))
			*obj = static_cast<IPlugView*>(this);
		else if (iidIs(iid, IPlugViewContentScaleSupport::iid))
			*obj = static_cast<IPlugViewContentScaleSupport*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return retain(); }
	uint32 PLUGIN_API release() override { return drop(); }

	tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
	{
		return type != nullptr && std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API attached(void* parent, FIDString type) override
	{
		if (isPlatformTypeSupported(type) != kResultOk || parent == nullptr)
			return kInvalidArgument;
		parentWindow = static_cast<HWND>(parent);
		wchar_t title[64];
		swprintf_s(title, L"TestVst3Plugin Gain %.2f Scale %.2f Zoom %.2f",
			state != nullptr ? state->gain : 1.0, contentScaleFactor,
			privateState != nullptr ? privateState->zoom : 1.0);
		window = CreateWindowExW(0, L"STATIC", title, WS_CHILD | WS_VISIBLE,
			0, 0, rect.getWidth(), rect.getHeight(), static_cast<HWND>(parent), nullptr, GetModuleHandleW(nullptr), nullptr);
		if (window == nullptr)
			return kResultFalse;
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
		originalWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(&TestView::windowProc)));
		if (frame != nullptr)
		{
			ViewRect requested(0, 0, 360, 220);
			frame->resizeView(this, &requested);
		}
		return kResultOk;
	}
	tresult PLUGIN_API removed() override
	{
		if (window != nullptr)
			DestroyWindow(window);
		window = nullptr;
		parentWindow = nullptr;
		return kResultOk;
	}
	tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
	tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
	tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
	tresult PLUGIN_API getSize(ViewRect* size) override
	{
		if (size == nullptr)
			return kInvalidArgument;
		*size = rect;
		return kResultOk;
	}
	tresult PLUGIN_API onSize(ViewRect* newSize) override
	{
		if (newSize == nullptr)
			return kInvalidArgument;
		if (parentWindow != nullptr)
		{
			RECT parentRect = {};
			GetClientRect(parentWindow, &parentRect);
			if (parentRect.right - parentRect.left != newSize->getWidth()
				|| parentRect.bottom - parentRect.top != newSize->getHeight())
				return kResultFalse;
		}
		rect = *newSize;
		if (window != nullptr)
			SetWindowPos(window, nullptr, 0, 0, rect.getWidth(), rect.getHeight(), SWP_NOACTIVATE | SWP_NOZORDER);
		return kResultOk;
	}
	tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
	tresult PLUGIN_API setFrame(IPlugFrame* newFrame) override
	{
		if (frame != nullptr)
			frame->release();
		frame = newFrame;
		if (frame != nullptr)
			frame->addRef();
		return kResultOk;
	}
	tresult PLUGIN_API canResize() override { return kResultOk; }
	tresult PLUGIN_API checkSizeConstraint(ViewRect* size) override
	{
		if (size == nullptr)
			return kInvalidArgument;
		size->right = size->left + std::max<int32>(160, size->getWidth());
		size->bottom = size->top + std::max<int32>(90, size->getHeight());
		return kResultOk;
	}
	tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override
	{
		contentScaleFactor = factor;
		return kResultOk;
	}

private:
	~TestView() override
	{
		removed();
		if (frame != nullptr)
			frame->release();
		if (handler != nullptr)
			handler->release();
	}

	static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		TestView* view = reinterpret_cast<TestView*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		if (view != nullptr && (message == WM_APP + 77 || message == WM_APP + 79))
		{
			if (view->handler != nullptr)
			{
				const ParamValue value = message == WM_APP + 79 ? 0.75 : 0.25;
				if (view->state != nullptr)
					view->state->gain = value;
				view->handler->beginEdit(gainParamId);
				view->handler->performEdit(gainParamId, value);
				view->handler->endEdit(gainParamId);
			}
			return 0;
		}
		if (view != nullptr && message == WM_APP + 78)
		{
			if (view->privateState != nullptr)
				view->privateState->zoom = 1.75;
			if (view->privateStateDirty != nullptr)
				*view->privateStateDirty = true;
			wchar_t title[96];
			swprintf_s(title, L"TestVst3Plugin Gain %.2f Scale %.2f Zoom %.2f",
				view->state != nullptr ? view->state->gain : 1.0,
				view->contentScaleFactor, view->privateState != nullptr ? view->privateState->zoom : 1.0);
			SetWindowTextW(window, title);
			if (view->handler != nullptr)
			{
				IComponentHandler2* extendedHandler = nullptr;
				if (view->handler->queryInterface(IComponentHandler2::iid,
					reinterpret_cast<void**>(&extendedHandler)) == kResultOk && extendedHandler != nullptr)
				{
					extendedHandler->setDirty(true);
					extendedHandler->release();
				}
			}
			return 0;
		}
		return view != nullptr && view->originalWindowProc != nullptr
			? CallWindowProcW(view->originalWindowProc, window, message, wParam, lParam)
			: DefWindowProcW(window, message, wParam, lParam);
	}

	ViewRect rect{0, 0, 320, 180};
	PluginState* state = nullptr;
	ScaleFactor contentScaleFactor = 1.0;
	IPlugFrame* frame = nullptr;
	IComponentHandler* handler = nullptr;
	ControllerPrivateState* privateState = nullptr;
	bool* privateStateDirty = nullptr;
	HWND parentWindow = nullptr;
	HWND window = nullptr;
	WNDPROC originalWindowProc = nullptr;
};

IEditController* createTestController();

class TestComponent final : public IComponent, public IAudioProcessor, public IEditController, private RefCounted
{
public:
	TestComponent(ComponentScenario scenario)
		: combinedController(scenario == ComponentScenario::combinedDouble),
		supportsDouble(scenario != ComponentScenario::splitFloat)
	{
		if (combinedController)
			controllerDelegate = createTestController();
	}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (obj == nullptr)
			return kInvalidArgument;
		if (iidIs(iid, FUnknown::iid) || iidIs(iid, IPluginBase::iid) || iidIs(iid, IComponent::iid))
			*obj = static_cast<IComponent*>(this);
		else if (iidIs(iid, IAudioProcessor::iid))
			*obj = static_cast<IAudioProcessor*>(this);
		else if (combinedController && iidIs(iid, IEditController::iid))
			*obj = static_cast<IEditController*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return retain(); }
	uint32 PLUGIN_API release() override { return drop(); }

	tresult PLUGIN_API initialize(FUnknown* context) override
	{
		if (rejectComponentInitialize)
			return kResultFalse;
		if (context == nullptr)
			return kResultFalse;
		if (initialized)
		{
			doubleInitialized = true;
			return kResultFalse;
		}
		IHostApplication* host = nullptr;
		if (context->queryInterface(IHostApplication::iid, reinterpret_cast<void**>(&host)) != kResultOk || host == nullptr)
			return kResultFalse;
		TUID messageIid;
		IMessage::iid.toTUID(messageIid);
		IMessage* message = nullptr;
		tresult result = host->createInstance(messageIid, messageIid, reinterpret_cast<void**>(&message));
		host->release();
		if (result != kResultOk || message == nullptr || message->getAttributes() == nullptr)
		{
			if (message != nullptr)
				message->release();
			return kResultFalse;
		}
		message->setMessageID("host-contract");
		message->getAttributes()->setInt("ready", 1);
		message->release();
		if (combinedController && controllerDelegate->initialize(context) != kResultOk)
			return kResultFalse;
		initialized = true;
		return kResultOk;
	}
	tresult PLUGIN_API terminate() override
	{
		if (!initialized)
		{
			HANDLE violation = OpenEventW(EVENT_MODIFY_STATE, FALSE, testvst3::invalidTerminateEvent);
			if (violation != nullptr)
			{
				SetEvent(violation);
				CloseHandle(violation);
			}
		}
		if (combinedController && initialized)
			controllerDelegate->terminate();
		initialized = false;
		return kResultOk;
	}
	tresult PLUGIN_API getControllerClassId(TUID classId) override
	{
		if (combinedController)
			return kNoInterface;
		copyTuid(classId, controllerCid);
		return kResultOk;
	}
	tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
	int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override { return type == kAudio ? 1 : 0; }
	tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo& info) override
	{
		if (type != kAudio || index != 0)
			return kInvalidArgument;
		std::memset(&info, 0, sizeof(info));
		info.mediaType = kAudio;
		info.direction = direction;
		info.channelCount = 2;
		info.busType = kMain;
		info.flags = BusInfo::kDefaultActive;
		copyString128(info.name, direction == kInput ? L"Stereo In" : L"Stereo Out");
		return kResultOk;
	}
	tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
	tresult PLUGIN_API activateBus(MediaType type, BusDirection, int32 index, TBool) override
	{
		return type == kAudio && index == 0 ? kResultOk : kInvalidArgument;
	}
	tresult PLUGIN_API setActive(TBool state) override { active.store(state != 0); return kResultOk; }
	tresult PLUGIN_API setState(IBStream* stream) override { return readState(stream, state) ? kResultOk : kResultFalse; }
	tresult PLUGIN_API getState(IBStream* stream) override
	{
		return componentStateUnavailable ? kNotImplemented : writeState(stream, state);
	}

	tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
		SpeakerArrangement* outputs, int32 numOuts) override
	{
		return numIns == 1 && numOuts == 1 && inputs != nullptr && outputs != nullptr
			&& inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API getBusArrangement(BusDirection, int32 index, SpeakerArrangement& arrangement) override
	{
		if (index != 0)
			return kInvalidArgument;
		arrangement = SpeakerArr::kStereo;
		return kResultOk;
	}
	tresult PLUGIN_API canProcessSampleSize(int32 size) override
	{
		return size == kSample32 || (supportsDouble && size == kSample64) ? kResultOk : kResultFalse;
	}
	uint32 PLUGIN_API getLatencySamples() override { return 0; }
	tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) override { setup = newSetup; return kResultOk; }
	tresult PLUGIN_API setProcessing(TBool state) override
	{
		if (state != 0 && zeroSampleFlushInProgress.load())
		{
			HANDLE violation = OpenEventW(EVENT_MODIFY_STATE, FALSE, testvst3::concurrentProcessingEvent);
			if (violation != nullptr)
			{
				SetEvent(violation);
				CloseHandle(violation);
			}
		}
		processing.store(state != 0);
		return active.load() ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API process(ProcessData& data) override
	{
		if (!processing.load() || data.symbolicSampleSize != setup.symbolicSampleSize)
			return kResultFalse;
		if (data.inputParameterChanges != nullptr)
		{
			for (int32 parameterIndex = 0; parameterIndex < data.inputParameterChanges->getParameterCount(); ++parameterIndex)
			{
				IParamValueQueue* queue = data.inputParameterChanges->getParameterData(parameterIndex);
				if (queue == nullptr || queue->getParameterId() != gainParamId || queue->getPointCount() == 0)
					continue;
				int32 sampleOffset = 0;
				ParamValue value = state.gain;
				if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) == kResultOk)
					state.gain = value;
			}
		}
		if (data.numSamples == 0 && data.numInputs == 0 && data.numOutputs == 0)
		{
			HANDLE entered = OpenEventW(EVENT_MODIFY_STATE, FALSE, testvst3::flushEnteredEvent);
			HANDLE proceed = OpenEventW(SYNCHRONIZE, FALSE, testvst3::flushContinueEvent);
			if (entered != nullptr && proceed != nullptr)
			{
				zeroSampleFlushInProgress.store(true);
				SetEvent(entered);
				WaitForSingleObject(proceed, 5000);
				zeroSampleFlushInProgress.store(false);
			}
			if (entered != nullptr)
				CloseHandle(entered);
			if (proceed != nullptr)
				CloseHandle(proceed);
			return kResultOk;
		}
		if (data.numInputs != 1 || data.numOutputs != 1 || data.inputs == nullptr || data.outputs == nullptr)
			return kResultFalse;
		for (int32 channel = 0; channel < 2; ++channel)
		{
			if (data.symbolicSampleSize == kSample64)
			{
				for (int32 sample = 0; sample < data.numSamples; ++sample)
					data.outputs[0].channelBuffers64[channel][sample] = data.inputs[0].channelBuffers64[channel][sample] * state.gain;
			}
			else
			{
				for (int32 sample = 0; sample < data.numSamples; ++sample)
					data.outputs[0].channelBuffers32[channel][sample] = static_cast<float>(data.inputs[0].channelBuffers32[channel][sample] * state.gain);
			}
		}
		return kResultOk;
	}
	uint32 PLUGIN_API getTailSamples() override { return kNoTail; }

	tresult PLUGIN_API setComponentState(IBStream* stream) override { return controllerDelegate->setComponentState(stream); }
	int32 PLUGIN_API getParameterCount() override { return controllerDelegate->getParameterCount(); }
	tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override { return controllerDelegate->getParameterInfo(index, info); }
	tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 string) override
	{
		return controllerDelegate->getParamStringByValue(id, value, string);
	}
	tresult PLUGIN_API getParamValueByString(ParamID id, TChar* string, ParamValue& value) override
	{
		return controllerDelegate->getParamValueByString(id, string, value);
	}
	ParamValue PLUGIN_API normalizedParamToPlain(ParamID id, ParamValue value) override
	{
		return controllerDelegate->normalizedParamToPlain(id, value);
	}
	ParamValue PLUGIN_API plainParamToNormalized(ParamID id, ParamValue value) override
	{
		return controllerDelegate->plainParamToNormalized(id, value);
	}
	ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return controllerDelegate->getParamNormalized(id); }
	tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
	{
		return controllerDelegate->setParamNormalized(id, value);
	}
	tresult PLUGIN_API setComponentHandler(IComponentHandler* handler) override
	{
		return controllerDelegate->setComponentHandler(handler);
	}
	IPlugView* PLUGIN_API createView(FIDString name) override
	{
		return !doubleInitialized ? controllerDelegate->createView(name) : nullptr;
	}

private:
	~TestComponent() override
	{
		if (controllerDelegate != nullptr)
			controllerDelegate->release();
	}
	bool combinedController = false;
	bool supportsDouble = true;
	bool doubleInitialized = false;
	IEditController* controllerDelegate = nullptr;
	bool initialized = false;
	std::atomic<bool> active{false};
	std::atomic<bool> processing{false};
	ProcessSetup setup{};
	PluginState state{};
};

/*
    Mirrors the bus contract of upmixer plugins such as the OpenSpatial
    Upmixer: the plugin wants one multichannel (7.1) bus pair, with only L/R
    of the input carrying signal, and fills the remaining physical channels
    itself. Like a typical JUCE build it also accepts a plain stereo layout,
    which is exactly what allows a host that only ever proposes stereo to
    silently degrade it into several blind stereo instances.
*/
class TestUpmixerComponent final : public IComponent, public IAudioProcessor, private RefCounted
{
public:
	TestUpmixerComponent()
	{
		++upmixerComponentCount;
	}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (obj == nullptr)
			return kInvalidArgument;
		if (iidIs(iid, FUnknown::iid) || iidIs(iid, IPluginBase::iid) || iidIs(iid, IComponent::iid))
			*obj = static_cast<IComponent*>(this);
		else if (iidIs(iid, IAudioProcessor::iid))
			*obj = static_cast<IAudioProcessor*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return retain(); }
	uint32 PLUGIN_API release() override { return drop(); }

	tresult PLUGIN_API initialize(FUnknown* context) override
	{
		if (context == nullptr || initialized)
			return kResultFalse;
		initialized = true;
		return kResultOk;
	}
	tresult PLUGIN_API terminate() override { initialized = false; return kResultOk; }
	tresult PLUGIN_API getControllerClassId(TUID) override { return kNoInterface; }
	tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
	int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override { return type == kAudio ? 1 : 0; }
	tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo& info) override
	{
		if (type != kAudio || index != 0)
			return kInvalidArgument;
		std::memset(&info, 0, sizeof(info));
		info.mediaType = kAudio;
		info.direction = direction;
		info.channelCount = SpeakerArr::getChannelCount(direction == kInput ? inputArrangement : outputArrangement);
		info.busType = kMain;
		info.flags = BusInfo::kDefaultActive;
		copyString128(info.name, direction == kInput ? L"Upmix In" : L"Upmix Out");
		return kResultOk;
	}
	tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
	tresult PLUGIN_API activateBus(MediaType type, BusDirection, int32 index, TBool) override
	{
		return type == kAudio && index == 0 ? kResultOk : kInvalidArgument;
	}
	tresult PLUGIN_API setActive(TBool state) override { active.store(state != 0); return kResultOk; }
	tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
	tresult PLUGIN_API getState(IBStream* stream) override
	{
		const uint32 marker = 0x584D5055; // UPMX
		return writeState(stream, marker);
	}

	tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
		SpeakerArrangement* outputs, int32 numOuts) override
	{
		if (numIns != 1 || numOuts != 1 || inputs == nullptr || outputs == nullptr)
			return kResultFalse;
		const auto isSurround = [](SpeakerArrangement a)
		{
			return a == SpeakerArr::k71Music || a == SpeakerArr::k71Cine;
		};
		// Accepted layouts mirror the real plugin: symmetric stereo, symmetric
		// 7.1 (accepted but the engine stays off there), and the DAW-style
		// stereo input bus feeding a 7.1 output bus.
		const bool symmetric = inputs[0] == outputs[0]
			&& (inputs[0] == SpeakerArr::kStereo || isSurround(inputs[0]));
		const bool stereoIntoSurround = inputs[0] == SpeakerArr::kStereo && isSurround(outputs[0]);
		if (!symmetric && !stereoIntoSurround)
			return kResultFalse;
		inputArrangement = inputs[0];
		outputArrangement = outputs[0];
		return kResultOk;
	}
	tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 index, SpeakerArrangement& current) override
	{
		if (index != 0)
			return kInvalidArgument;
		current = direction == kInput ? inputArrangement : outputArrangement;
		return kResultOk;
	}
	tresult PLUGIN_API canProcessSampleSize(int32 size) override
	{
		return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
	}
	uint32 PLUGIN_API getLatencySamples() override { return 0; }
	tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) override { setup = newSetup; return kResultOk; }
	tresult PLUGIN_API setProcessing(TBool state) override
	{
		processing.store(state != 0);
		return active.load() ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API process(ProcessData& data) override
	{
		if (data.numSamples == 0 && data.numInputs == 0 && data.numOutputs == 0)
			return kResultOk;
		if (!processing.load() || data.symbolicSampleSize != setup.symbolicSampleSize)
			return kResultFalse;
		const int32 inputChannels = SpeakerArr::getChannelCount(inputArrangement);
		const int32 outputChannels = SpeakerArr::getChannelCount(outputArrangement);
		// The host must hand over the full negotiated bus widths; anything
		// narrower means it broke the arrangement contract.
		if (data.numInputs != 1 || data.numOutputs != 1 || data.inputs == nullptr || data.outputs == nullptr
			|| data.inputs[0].numChannels != inputChannels || data.outputs[0].numChannels != outputChannels)
			return kResultFalse;
		// Like the real plugin, the upmix engine runs only on the DAW-style
		// layout (stereo input bus, multichannel output bus). On a symmetric
		// multichannel layout the plugin accepts the buses but passes the
		// front pair through and leaves every other channel silent - the
		// exact behavior the probe measured on the OpenSpatial Upmixer.
		const bool engineEngaged = inputChannels == 2 && outputChannels == 8;
		for (int32 sample = 0; sample < data.numSamples; ++sample)
		{
			const double left = data.symbolicSampleSize == kSample64
				? data.inputs[0].channelBuffers64[0][sample] : data.inputs[0].channelBuffers32[0][sample];
			const double right = data.symbolicSampleSize == kSample64
				? data.inputs[0].channelBuffers64[1][sample] : data.inputs[0].channelBuffers32[1][sample];
			double values[8] = {left, right, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
			if (engineEngaged)
			{
				// Distinct per-channel markers so the host test can verify
				// both that every channel is produced and that it lands on
				// the right physical output: L R C LFE RL RR SL SR.
				values[2] = left + right;
				values[3] = 0.125 * (left + right);
				values[4] = 0.25 * left;
				values[5] = 0.25 * right;
				values[6] = 0.5 * left;
				values[7] = 0.5 * right;
			}
			for (int32 channel = 0; channel < outputChannels; ++channel)
			{
				if (data.symbolicSampleSize == kSample64)
					data.outputs[0].channelBuffers64[channel][sample] = values[channel];
				else
					data.outputs[0].channelBuffers32[channel][sample] = static_cast<float>(values[channel]);
			}
		}
		return kResultOk;
	}
	uint32 PLUGIN_API getTailSamples() override { return kNoTail; }

private:
	~TestUpmixerComponent() override = default;

	bool initialized = false;
	std::atomic<bool> active{false};
	std::atomic<bool> processing{false};
	ProcessSetup setup{};
	SpeakerArrangement inputArrangement = SpeakerArr::k71Music;
	SpeakerArrangement outputArrangement = SpeakerArr::k71Music;
};

// Surround41.vst3 mode: a symmetric component that accepts stereo, 4.1 Music
// and 5.0 layouts, records the accepted arrangement for the host tests, and
// writes a distinct constant into every output slot keyed by that slot's
// speaker role - so a test can prove which EAPO channel received which role.
class TestSurround41Component final : public IComponent, public IAudioProcessor, private RefCounted
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (obj == nullptr)
			return kInvalidArgument;
		if (iidIs(iid, FUnknown::iid) || iidIs(iid, IPluginBase::iid) || iidIs(iid, IComponent::iid))
			*obj = static_cast<IComponent*>(this);
		else if (iidIs(iid, IAudioProcessor::iid))
			*obj = static_cast<IAudioProcessor*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return retain(); }
	uint32 PLUGIN_API release() override { return drop(); }

	tresult PLUGIN_API initialize(FUnknown* context) override
	{
		if (context == nullptr || initialized)
			return kResultFalse;
		initialized = true;
		return kResultOk;
	}
	tresult PLUGIN_API terminate() override { initialized = false; return kResultOk; }
	tresult PLUGIN_API getControllerClassId(TUID) override { return kNoInterface; }
	tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
	int32 PLUGIN_API getBusCount(MediaType type, BusDirection) override { return type == kAudio ? 1 : 0; }
	tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo& info) override
	{
		if (type != kAudio || index != 0)
			return kInvalidArgument;
		std::memset(&info, 0, sizeof(info));
		info.mediaType = kAudio;
		info.direction = direction;
		info.channelCount = SpeakerArr::getChannelCount(arrangement);
		info.busType = kMain;
		info.flags = BusInfo::kDefaultActive;
		copyString128(info.name, direction == kInput ? L"Surround In" : L"Surround Out");
		return kResultOk;
	}
	tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
	tresult PLUGIN_API activateBus(MediaType type, BusDirection, int32 index, TBool) override
	{
		return type == kAudio && index == 0 ? kResultOk : kInvalidArgument;
	}
	tresult PLUGIN_API setActive(TBool state) override { active.store(state != 0); return kResultOk; }
	tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
	tresult PLUGIN_API getState(IBStream* stream) override
	{
		const uint32 marker = 0x31345253; // SR41
		return writeState(stream, marker);
	}

	tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
		SpeakerArrangement* outputs, int32 numOuts) override
	{
		if (numIns != 1 || numOuts != 1 || inputs == nullptr || outputs == nullptr
			|| inputs[0] != outputs[0])
			return kResultFalse;
		if (outputs[0] != SpeakerArr::kStereo
			&& outputs[0] != SpeakerArr::k41Music
			&& outputs[0] != SpeakerArr::k50)
			return kResultFalse;
		arrangement = outputs[0];
		surround41AcceptedOutputArrangement.store(
			static_cast<unsigned long long>(arrangement));
		return kResultOk;
	}
	tresult PLUGIN_API getBusArrangement(BusDirection, int32 index, SpeakerArrangement& current) override
	{
		if (index != 0)
			return kInvalidArgument;
		current = arrangement;
		return kResultOk;
	}
	tresult PLUGIN_API canProcessSampleSize(int32 size) override
	{
		return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
	}
	uint32 PLUGIN_API getLatencySamples() override { return 0; }
	tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) override { setup = newSetup; return kResultOk; }
	tresult PLUGIN_API setProcessing(TBool state) override
	{
		processing.store(state != 0);
		return active.load() ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API process(ProcessData& data) override
	{
		if (data.numSamples == 0 && data.numInputs == 0 && data.numOutputs == 0)
			return kResultOk;
		if (!processing.load() || data.symbolicSampleSize != setup.symbolicSampleSize)
			return kResultFalse;

		const int32 channelCount = SpeakerArr::getChannelCount(arrangement);
		if (data.numInputs != 1 || data.numOutputs != 1 || data.inputs == nullptr || data.outputs == nullptr
			|| data.inputs[0].numChannels != channelCount || data.outputs[0].numChannels != channelCount)
			return kResultFalse;

		const struct
		{
			Speaker speaker = 0;
			double value = 0.0;
		} roles[] = {
			{kSpeakerL, 0.125},
			{kSpeakerR, 0.25},
			{kSpeakerC, 0.75},
			{kSpeakerLfe, 0.5},
			{kSpeakerLs, 0.375},
			{kSpeakerRs, 0.4375}
		};

		for (int32 channel = 0; channel < channelCount; channel++)
		{
			double value = 0.0;
			for (const auto& role : roles)
			{
				if (SpeakerArr::getSpeakerIndex(role.speaker, arrangement) == channel)
				{
					value = role.value;
					break;
				}
			}

			if (data.symbolicSampleSize == kSample64)
			{
				for (int32 sample = 0; sample < data.numSamples; sample++)
					data.outputs[0].channelBuffers64[channel][sample] = value;
			}
			else
			{
				for (int32 sample = 0; sample < data.numSamples; sample++)
					data.outputs[0].channelBuffers32[channel][sample] = static_cast<float>(value);
			}
		}
		return kResultOk;
	}
	uint32 PLUGIN_API getTailSamples() override { return kNoTail; }

private:
	~TestSurround41Component() override = default;

	bool initialized = false;
	std::atomic<bool> active{false};
	std::atomic<bool> processing{false};
	ProcessSetup setup{};
	SpeakerArrangement arrangement = SpeakerArr::kStereo;
};

class TestController final : public IEditController, private RefCounted
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (obj == nullptr)
			return kInvalidArgument;
		if (iidIs(iid, FUnknown::iid) || iidIs(iid, IPluginBase::iid) || iidIs(iid, IEditController::iid))
			*obj = static_cast<IEditController*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return retain(); }
	uint32 PLUGIN_API release() override { return drop(); }

	tresult PLUGIN_API initialize(FUnknown* context) override
	{
		if (context == nullptr || initialized)
			return kResultFalse;
		IPlugInterfaceSupport* support = nullptr;
		hasHostInterfaceSupport = context->queryInterface(IPlugInterfaceSupport::iid,
			reinterpret_cast<void**>(&support)) == kResultOk && support != nullptr;
		if (support != nullptr)
		{
			hasHostInterfaceSupport = support->isPlugInterfaceSupported(IPlugView::iid) == kResultTrue;
			support->release();
		}
		if (!hasHostInterfaceSupport)
			return kResultFalse;
		initialized = true;
		return kResultOk;
	}
	tresult PLUGIN_API terminate() override { initialized = false; return kResultOk; }
	tresult PLUGIN_API setComponentState(IBStream* stream) override { return readState(stream, state) ? kResultOk : kResultFalse; }
	// Controller-private state deliberately has no component gain. A host must
	// call setComponentState when restoring audio state so the GUI stays in sync.
	tresult PLUGIN_API setState(IBStream* stream) override
	{
		privateStateDirty = readControllerState(stream, privateState);
		return privateStateDirty ? kResultOk : kResultFalse;
	}
	tresult PLUGIN_API getState(IBStream* stream) override
	{
		return privateStateDirty ? writeState(stream, privateState) : kNotImplemented;
	}
	int32 PLUGIN_API getParameterCount() override { return 1; }
	tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override
	{
		if (index != 0)
			return kInvalidArgument;
		std::memset(&info, 0, sizeof(info));
		info.id = gainParamId;
		copyString128(info.title, L"Gain");
		copyString128(info.shortTitle, L"Gain");
		info.defaultNormalizedValue = 1.0;
		info.flags = ParameterInfo::kCanAutomate;
		return kResultOk;
	}
	tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 string) override
	{
		if (id != gainParamId)
			return kInvalidArgument;
		swprintf_s(reinterpret_cast<wchar_t*>(string), 128, L"%.2f", value);
		return kResultOk;
	}
	tresult PLUGIN_API getParamValueByString(ParamID id, TChar* string, ParamValue& value) override
	{
		if (id != gainParamId || string == nullptr)
			return kInvalidArgument;
		value = wcstod(reinterpret_cast<wchar_t*>(string), nullptr);
		return kResultOk;
	}
	ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue value) override { return value; }
	ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue value) override { return value; }
	ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return id == gainParamId ? state.gain : 0.0; }
	tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
	{
		if (id != gainParamId)
			return kInvalidArgument;
		state.gain = value;
		return kResultOk;
	}
	tresult PLUGIN_API setComponentHandler(IComponentHandler* newHandler) override
	{
		if (handler != nullptr)
			handler->release();
		handler = newHandler;
		if (handler != nullptr)
		{
			handler->addRef();
			IComponentHandler2* extendedHandler = nullptr;
			hasExtendedHandler = handler->queryInterface(IComponentHandler2::iid,
				reinterpret_cast<void**>(&extendedHandler)) == kResultOk && extendedHandler != nullptr;
			if (extendedHandler != nullptr)
				extendedHandler->release();
		}
		else
			hasExtendedHandler = false;
		return hasExtendedHandler || handler == nullptr ? kResultOk : kResultFalse;
	}
	IPlugView* PLUGIN_API createView(FIDString name) override
	{
		return hasHostInterfaceSupport && hasExtendedHandler && name != nullptr && std::strcmp(name, ViewType::kEditor) == 0
			? new TestView(&state, handler, &privateState, &privateStateDirty) : nullptr;
	}

private:
	~TestController() override
	{
		if (handler != nullptr)
			handler->release();
	}
	bool initialized = false;
	bool hasHostInterfaceSupport = false;
	IComponentHandler* handler = nullptr;
	bool hasExtendedHandler = false;
	PluginState state{};
	ControllerPrivateState privateState{};
	bool privateStateDirty = false;
};

IEditController* createTestController()
{
	return new TestController();
}

class TestFactory final : public IPluginFactory3
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		if (obj == nullptr)
			return kInvalidArgument;
		if (iidIs(iid, FUnknown::iid) || iidIs(iid, IPluginFactory::iid))
			*obj = static_cast<IPluginFactory*>(this);
		else if (iidIs(iid, IPluginFactory2::iid))
			*obj = static_cast<IPluginFactory2*>(this);
		else if (iidIs(iid, IPluginFactory3::iid))
			*obj = static_cast<IPluginFactory3*>(this);
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}
	uint32 PLUGIN_API addRef() override { return ++refCount; }
	uint32 PLUGIN_API release() override { return --refCount; }
	tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
	{
		if (info == nullptr)
			return kInvalidArgument;
		*info = PFactoryInfo("EqualizerAPO-XT Tests", "https://example.invalid", "test@example.invalid", PFactoryInfo::kNoFlags);
		return kResultOk;
	}
	int32 PLUGIN_API countClasses() override { return 2; }
	tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override
	{
		if (info == nullptr || index < 0 || index > 1)
			return kInvalidArgument;
		*info = index == 0
			? PClassInfo(componentCid, PClassInfo::kManyInstances, kVstAudioEffectClass, "TestVst3Plugin")
			: PClassInfo(controllerCid, PClassInfo::kManyInstances, kVstComponentControllerClass, "TestVst3Controller");
		return kResultOk;
	}
	tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override
	{
		if (cid == nullptr || iid == nullptr || obj == nullptr)
			return kInvalidArgument;
		*obj = nullptr;
		if (!hostContextSet)
			return kResultFalse;
		FUnknown* instance = nullptr;
		if (FUnknownPrivate::iidEqual(cid, componentCid))
		{
			if (surround41Mode)
				instance = static_cast<IComponent*>(new TestSurround41Component());
			else if (upmixerMode)
				instance = static_cast<IComponent*>(new TestUpmixerComponent());
			else
			{
				const ComponentScenario scenarios[] = {
					ComponentScenario::splitDouble,
					ComponentScenario::combinedDouble,
					ComponentScenario::splitFloat
				};
				instance = static_cast<IComponent*>(new TestComponent(scenarios[componentInstanceCount++ % 3]));
			}
		}
		else if (FUnknownPrivate::iidEqual(cid, controllerCid))
			instance = static_cast<IEditController*>(new TestController());
		else
			return kNoInterface;
		tresult result = instance->queryInterface(iid, obj);
		instance->release();
		return result;
	}
	tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
	{
		if (info == nullptr || index < 0 || index > 1)
			return kInvalidArgument;
		PClassInfo basic;
		getClassInfo(index, &basic);
		std::memset(info, 0, sizeof(*info));
		copyTuid(info->cid, basic.cid);
		info->cardinality = basic.cardinality;
		strcpy_s(info->category, basic.category);
		strcpy_s(info->name, basic.name);
		strcpy_s(info->subCategories, index == 0 ? "Fx" : "");
		strcpy_s(info->vendor, "EqualizerAPO-XT Tests");
		strcpy_s(info->version, "1.0.0");
		strcpy_s(info->sdkVersion, "VST 3.8");
		return kResultOk;
	}
	tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override
	{
		if (info == nullptr)
			return kInvalidArgument;
		PClassInfo2 ascii;
		if (getClassInfo2(index, &ascii) != kResultOk)
			return kInvalidArgument;
		info->fromAscii(ascii);
		return kResultOk;
	}
	tresult PLUGIN_API setHostContext(FUnknown* context) override
	{
		if (context == nullptr)
		{
			hostContextSet = false;
			return kResultOk;
		}
		if (hostContextSetCount++ != 0)
			return kResultFalse;
		IHostApplication* host = nullptr;
		hostContextSet = context->queryInterface(IHostApplication::iid, reinterpret_cast<void**>(&host)) == kResultOk
			&& host != nullptr;
		if (host != nullptr)
		{
			TUID messageIid;
			IMessage::iid.toTUID(messageIid);
			IMessage* message = nullptr;
			hostContextSet = hostContextSet
				&& host->createInstance(messageIid, messageIid, reinterpret_cast<void**>(&message)) == kResultOk
				&& message != nullptr && message->getAttributes() != nullptr;
			if (message != nullptr)
				message->release();
			host->release();
		}
		return hostContextSet ? kResultOk : kResultFalse;
	}

private:
	std::atomic<uint32> refCount{1};
	bool hostContextSet = false;
	int hostContextSetCount = 0;
	int componentInstanceCount = 0;
};

TestFactory factory;
}

extern "C" __declspec(dllexport) bool InitDll()
{
	HMODULE currentModule = nullptr;
	wchar_t modulePath[MAX_PATH] = {};
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&InitDll), &currentModule) != FALSE)
		GetModuleFileNameW(currentModule, modulePath, MAX_PATH);
	if (wcsstr(modulePath, L"RejectInit.vst3") != nullptr)
		return false;
	wcscpy_s(loadedModulePath, modulePath);
	rejectComponentInitialize = wcsstr(modulePath, L"RejectComponent.vst3") != nullptr;
	componentStateUnavailable = wcsstr(modulePath, L"ControllerState.vst3") != nullptr;
	upmixerMode = wcsstr(modulePath, L"Upmixer.vst3") != nullptr;
	surround41Mode = wcsstr(modulePath, L"Surround41.vst3") != nullptr;
	surround41AcceptedOutputArrangement.store(
		static_cast<unsigned long long>(SpeakerArr::kStereo));
	moduleInitialized = true;
	return true;
}

extern "C" __declspec(dllexport) bool ExitDll()
{
	if (loadedModulePath[0] != L'\0')
	{
		wchar_t markerPath[MAX_PATH + 6] = {};
		swprintf_s(markerPath, L"%s.exit", loadedModulePath);
		HANDLE marker = CreateFileW(markerPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (marker != INVALID_HANDLE_VALUE)
			CloseHandle(marker);
	}
	moduleInitialized = false;
	rejectComponentInitialize = false;
	componentStateUnavailable = false;
	upmixerMode = false;
	surround41Mode = false;
	return true;
}

// In-process test hook: the host test asserts that a multichannel device is
// served by exactly one full-width component instance rather than several
// stereo copies.
extern "C" __declspec(dllexport) int GetUpmixerComponentCount()
{
	return upmixerComponentCount.load();
}

// In-process test hook for Surround41.vst3 mode: returns the speaker mask of
// the output arrangement the component last accepted, so the host test can
// prove which layout the negotiation actually landed on.
extern "C" __declspec(dllexport) unsigned long long GetSurround41AcceptedOutputArrangement()
{
	return surround41AcceptedOutputArrangement.load();
}

extern "C" __declspec(dllexport) IPluginFactory* PLUGIN_API GetPluginFactory()
{
	if (!moduleInitialized)
		return nullptr;
	factory.addRef();
	return &factory;
}
