/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <atomic>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "vst/VST3Lifecycle.h"
#include "Tests/TestHarness.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using std::string;
using std::vector;

namespace
{
	test::Harness harness("VST3LifecycleTests");

	class FakePlugin final : public IComponent, public IAudioProcessor
	{
	public:
		tresult PLUGIN_API queryInterface(const TUID iid, void** object) override
		{
			if (object == nullptr)
				return kInvalidArgument;
			if (FUnknownPrivate::iidEqual(iid, FUnknown::iid)
				|| FUnknownPrivate::iidEqual(iid, IPluginBase::iid)
				|| FUnknownPrivate::iidEqual(iid, IComponent::iid))
			{
				*object = static_cast<IComponent*>(this);
			}
			else if (FUnknownPrivate::iidEqual(iid, IAudioProcessor::iid))
				*object = static_cast<IAudioProcessor*>(this);
			else
			{
				*object = nullptr;
				return kNoInterface;
			}
			addRef();
			return kResultOk;
		}
		uint32 PLUGIN_API addRef() override { return ++references; }
		uint32 PLUGIN_API release() override { return --references; }

		tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
		tresult PLUGIN_API terminate() override { return kResultOk; }
		tresult PLUGIN_API getControllerClassId(TUID) override { return kNoInterface; }
		tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
		int32 PLUGIN_API getBusCount(MediaType, BusDirection) override { return 1; }
		tresult PLUGIN_API getBusInfo(MediaType, BusDirection, int32, BusInfo&) override { return kResultOk; }
		tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
		tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
		tresult PLUGIN_API setActive(TBool state) override
		{
			calls.push_back(state ? "active on" : "active off");
			active = state != 0;
			invokeTransitionCallback();
			return kResultOk;
		}
		tresult PLUGIN_API setState(IBStream*) override { return kResultOk; }
		tresult PLUGIN_API getState(IBStream*) override { return kResultOk; }

		tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32,
			SpeakerArrangement*, int32) override { return kResultOk; }
		tresult PLUGIN_API getBusArrangement(BusDirection, int32,
			SpeakerArrangement& arrangement) override
		{
			arrangement = SpeakerArr::kStereo;
			return kResultOk;
		}
		tresult PLUGIN_API canProcessSampleSize(int32) override { return kResultOk; }
		uint32 PLUGIN_API getLatencySamples() override { return 0; }
		tresult PLUGIN_API setupProcessing(ProcessSetup&) override
		{
			calls.push_back("setup");
			setupWhileActive = setupWhileActive || active;
			setupCalls++;
			invokeTransitionCallback();
			return kResultOk;
		}
		tresult PLUGIN_API setProcessing(TBool state) override
		{
			calls.push_back(state ? "processing on" : "processing off");
			processing = state != 0;
			invokeTransitionCallback();
			return active ? kResultOk : kResultFalse;
		}
		tresult PLUGIN_API process(ProcessData&) override
		{
			calls.push_back("process");
			processCalls++;
			return processing ? kResultOk : kResultFalse;
		}
		uint32 PLUGIN_API getTailSamples() override { return kNoTail; }

		void onTransition(const string& call, std::function<void()> callback)
		{
			transitionCall = call;
			transitionCallback = std::move(callback);
		}

		vector<string> calls;
		int setupCalls = 0;
		int processCalls = 0;
		bool setupWhileActive = false;
		bool active = false;
		bool processing = false;

	private:
		void invokeTransitionCallback()
		{
			if (!transitionCallback || transitionCall != calls.back())
				return;
			auto callback = std::move(transitionCallback);
			transitionCallback = nullptr;
			transitionCall.clear();
			callback();
		}

		std::atomic<uint32> references{1};
		string transitionCall;
		std::function<void()> transitionCallback;
	};

	ProcessSetup setup()
	{
		ProcessSetup value;
		value.processMode = kRealtime;
		value.symbolicSampleSize = kSample64;
		value.maxSamplesPerBlock = 256;
		value.sampleRate = 48000.0;
		return value;
	}

	void testSetupNeverRunsWhileActive()
	{
		FakePlugin plugin;
		VST3Lifecycle lifecycle;
		lifecycle.setInterfaces(&plugin, &plugin);
		harness.expectTrue(lifecycle.setupProcessing(setup()), "initial setup succeeds");
		harness.expectTrue(lifecycle.activate(), "component activates");
		harness.expectFalse(lifecycle.setupProcessing(setup()), "setup is refused while active");
		harness.expectEqual(plugin.setupCalls, 1, "active setup did not reach the processor");
		harness.expectFalse(plugin.setupWhileActive, "setupProcessing was never called while active");
		lifecycle.deactivate();
	}

	void testPrepareOnce()
	{
		FakePlugin plugin;
		VST3Lifecycle lifecycle;
		lifecycle.setInterfaces(&plugin, &plugin);
		harness.expectTrue(lifecycle.prepareOnce(kSample64), "first prepare-once succeeds");
		harness.expectTrue(lifecycle.prepareOnce(kSample64), "second prepare-once is already satisfied");
		harness.expectEqual(plugin.setupCalls, 1, "prepare-once calls setup exactly once");
	}

	void testEditorFlushDoesNotReactivate()
	{
		FakePlugin plugin;
		VST3Lifecycle lifecycle;
		lifecycle.setInterfaces(&plugin, &plugin);
		harness.expectTrue(lifecycle.beginEditorSession(kSample64), "editor session begins");
		plugin.calls.clear();
		bool pending = true;
		ProcessData data;
		data.symbolicSampleSize = kSample64;
		harness.expectTrue(lifecycle.flushParameters(data,
			[&pending]() { return pending; },
			[&pending]() -> IParameterChanges* { pending = false; return nullptr; },
			kSample64), "editor-session flush runs");
		harness.expectTrue(plugin.calls == vector<string>({"process"}),
			"editor-session flush neither activates nor changes Processing state");
		lifecycle.endEditorSession();
	}

	void testTransitionEditStaysQueued()
	{
		FakePlugin plugin;
		VST3Lifecycle lifecycle;
		lifecycle.setInterfaces(&plugin, &plugin);
		bool pending = false;
		ProcessData data;
		data.symbolicSampleSize = kSample64;
		const auto flush = [&]() {
			return lifecycle.flushParameters(data,
				[&pending]() { return pending; },
				[&pending]() -> IParameterChanges* { pending = false; return nullptr; },
				kSample64);
		};
		plugin.onTransition("active on", [&]() {
			pending = true;
			flush();
		});
		harness.expectTrue(lifecycle.beginEditorSession(kSample64),
			"editor transition succeeds after synchronous performEdit");
		harness.expectTrue(plugin.calls == vector<string>({"setup", "active on", "processing on"}),
			"editor session prepares, activates, then enters Processing state");
		harness.expectTrue(pending, "an edit during the transition stays queued");
		harness.expectEqual(plugin.processCalls, 0,
			"the nested transition edit does not process recursively");
		harness.expectTrue(flush(), "the next flush delivers the queued transition edit");
		harness.expectFalse(pending, "the delivered transition edit leaves the queue");
		harness.expectEqual(plugin.processCalls, 1, "the queued edit is delivered once");
		lifecycle.endEditorSession();
	}

	void testCanProcessNow()
	{
		FakePlugin plugin;
		VST3Lifecycle lifecycle;
		lifecycle.setInterfaces(&plugin, &plugin);
		harness.expectFalse(lifecycle.canProcessNow(), "a new lifecycle cannot process");
		harness.expectTrue(lifecycle.setupProcessing(setup()), "audio setup succeeds");
		harness.expectTrue(lifecycle.activate(), "activation alone succeeds");
		harness.expectFalse(lifecycle.canProcessNow(), "activation alone cannot process");
		lifecycle.deactivate();
		harness.expectTrue(lifecycle.startProcessing(), "audio processing starts");
		harness.expectTrue(lifecycle.canProcessNow(), "audio Processing state can process");
		lifecycle.stopProcessing();
		harness.expectFalse(lifecycle.canProcessNow(), "stopped audio cannot process");
		harness.expectTrue(lifecycle.beginEditorSession(kSample64), "editor session starts");
		harness.expectTrue(lifecycle.canProcessNow(), "editor Processing state can process");
		lifecycle.endEditorSession();
		harness.expectFalse(lifecycle.canProcessNow(), "ended editor session cannot process");
	}
}

void runVST3LifecycleTests()
{
	testSetupNeverRunsWhileActive();
	testPrepareOnce();
	testEditorFlushDoesNotReactivate();
	testTransitionEditStaysQueued();
	testCanProcessNow();
	harness.report();
}
