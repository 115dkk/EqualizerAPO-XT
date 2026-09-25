/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

// The VST3 processing side of VST3Instance: the parameter-edit ring, the
// lifecycle calls around it, and the process group. Moved here from
// VSTPluginInstance.cpp when the instance was split by format (audit #348
// C4/TD-40).

#include "stdafx.h"
#include "VSTPluginLibrary.h"
#include "VST3Instance.h"
#include "VST3HostContext.h"
#include "pluginterfaces/vst/ivstevents.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
// The queryInterface answer of the host-owned objects below, whose lifetime
// is the instance's and whose refcount is therefore vestigial. Spelled
// without the SDK's QUERY_INTERFACE macro, which cppcheck cannot expand.
template<typename Interface, typename Object>
tresult answerOwnedInterface(Object* object, const TUID iid, void** obj)
{
	if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) || FUnknownPrivate::iidEqual(iid, Interface::iid))
	{
		*obj = static_cast<Interface*>(object);
		return kResultOk;
	}
	*obj = NULL;
	return kNoInterface;
}

class EmptyVST3EventList : public IEventList
{
public:
	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		return answerOwnedInterface<IEventList>(this, iid, obj);
	}

	uint32 PLUGIN_API addRef() override { return 2; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API getEventCount() override { return 0; }
	tresult PLUGIN_API getEvent(int32, Event&) override { return kInvalidArgument; }
	tresult PLUGIN_API addEvent(Event&) override { return kResultFalse; }
};

EmptyVST3EventList emptyVST3EventList;

// The one VST3 process-call choreography behind both sample widths (audit
// #275 TD-25): the float and double bodies were 35-line near-clones whose
// only differences were the buffer field and the symbolic sample size.
template<typename SampleType>
struct Vst3SampleTraits;

template<>
struct Vst3SampleTraits<float>
{
	static constexpr int32 symbolicSampleSize = kSample32;
	static void attach(AudioBusBuffers& buffers, float** channels) { buffers.channelBuffers32 = channels; }
};

template<>
struct Vst3SampleTraits<double>
{
	static constexpr int32 symbolicSampleSize = kSample64;
	static void attach(AudioBusBuffers& buffers, double** channels) { buffers.channelBuffers64 = channels; }
};
}

// The parameter-change list handed to IAudioProcessor::process. Fixed-size
// and allocation-free after construction, so filling it on the audio thread
// stays RT-safe. Each parameter gets a single-point queue (sample offset 0):
// this host forwards GUI edits, not sample-accurate automation curves.
// Ported from the ripDZL fork's VST3 compatibility work
// (github.com/ripDZL/EqualizerAPO-XT/pull/1).
class VST3Instance::VST3ParameterChanges : public IParameterChanges
{
public:
	class ParamValueQueue : public IParamValueQueue
	{
	public:
		tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
		{
			return answerOwnedInterface<IParamValueQueue>(this, iid, obj);
		}

		// Embedded in the owning list; lifetime is the instance's.
		uint32 PLUGIN_API addRef() override { return 2; }
		uint32 PLUGIN_API release() override { return 1; }
		ParamID PLUGIN_API getParameterId() override { return id; }
		int32 PLUGIN_API getPointCount() override { return hasPoint ? 1 : 0; }
		tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& pointValue) override
		{
			if (!hasPoint || index != 0)
				return kInvalidArgument;
			sampleOffset = 0;
			pointValue = value;
			return kResultOk;
		}
		tresult PLUGIN_API addPoint(int32, ParamValue pointValue, int32& index) override
		{
			// Later points collapse onto the single slot: the last value of a
			// block wins, which is the semantic a control edit needs.
			value = pointValue;
			hasPoint = true;
			index = 0;
			return kResultOk;
		}

		void set(ParamID parameterId, ParamValue pointValue)
		{
			id = parameterId;
			value = pointValue;
			hasPoint = true;
		}

	private:
		ParamID id = 0;
		ParamValue value = 0.0;
		bool hasPoint = false;
	};

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
	{
		return answerOwnedInterface<IParameterChanges>(this, iid, obj);
	}

	// Owned by the instance through a unique_ptr; refcounting is vestigial.
	uint32 PLUGIN_API addRef() override { return 2; }
	uint32 PLUGIN_API release() override { return 1; }
	int32 PLUGIN_API getParameterCount() override { return count; }
	IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
	{
		return index >= 0 && index < count ? &queues[index] : NULL;
	}
	IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override
	{
		for (int32 i = 0; i < count; i++)
		{
			if (queues[i].getParameterId() == id)
			{
				index = i;
				return &queues[i];
			}
		}
		if (count >= (int32)queues.size())
		{
			index = -1;
			return NULL;
		}
		index = count++;
		queues[index].set(id, 0.0);
		return &queues[index];
	}

	void clear() { count = 0; }
	void add(ParamID id, ParamValue value)
	{
		int32 index;
		IParamValueQueue* queue = addParameterData(id, index);
		if (queue != NULL)
			queue->addPoint(0, value, index);
	}

private:
	array<ParamValueQueue, VST3Instance::vst3ParameterEditQueueSize> queues{};
	int32 count = 0;
};

std::unique_ptr<VSTFormatInstance> createVST3Instance(
	const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
{
	return std::make_unique<VST3Instance>(library, processLevel);
}

VST3Instance::VST3Instance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
	: VSTFormatInstance(library, processLevel), vst3InputParameterChanges(new VST3ParameterChanges())
{
}

VST3Instance::~VST3Instance()
{
	releaseVST3();
}

void VST3Instance::onVST3ParameterEdit(ParamID id, ParamValue value)
{
	queueVST3ParameterEdit(id, value);
	const bool processing = vst3Lifecycle.audioProcessing();
	flushVST3ParameterChanges();
	// While audio is running, the component does not own this value until its
	// next process call drains inputParameterChanges; saving synchronously
	// here would persist the previous component state. Editor instances are
	// stopped and take the immediate path below.
	if (!processing && !vst3Lifecycle.audioProcessing()
		&& vst3ParameterEditRead.load(memory_order_acquire) == vst3ParameterEditWrite.load(memory_order_acquire))
		onAutomate();
}

bool VST3Instance::queueVST3ParameterEdit(ParamID id, ParamValue value)
{
	// Single-producer ring: every writer runs on the instance's control
	// thread (the Editor's GUI thread for performEdit and writeToEffect; in
	// the engine, the configuration loader before processing starts). The
	// consumer is whoever runs the next process call. A full ring refuses
	// the edit. For a GUI edit that is harmless - the following edit of the
	// same control supersedes it; a state restore drains the ring and
	// retries instead (queueRestoredParameterEdit).
	unsigned writeIndex = vst3ParameterEditWrite.load(memory_order_relaxed);
	unsigned nextWriteIndex = (writeIndex + 1) % vst3ParameterEditQueueSize;
	if (nextWriteIndex == vst3ParameterEditRead.load(memory_order_acquire))
		return false;
	vst3ParameterEditQueue[writeIndex] = { id, value };
	vst3ParameterEditWrite.store(nextWriteIndex, memory_order_release);
	return true;
}

bool VST3Instance::queueRestoredParameterEdit(ParamID id, ParamValue value)
{
	// A saved state can carry more values than the ring holds (audit #348
	// TD-48: the 1024th and later were dropped without a word). The restore
	// runs on the control thread, so a full ring is drained right here with
	// the idle flush and the value queued again. When the flush cannot run -
	// audio is running and the ring waits for the next block, or a lifecycle
	// transition holds the flush - the value is refused and the caller
	// counts it (no allocation or wait on the audio thread either way).
	if (queueVST3ParameterEdit(id, value))
		return true;
	flushVST3ParameterChanges();
	return queueVST3ParameterEdit(id, value);
}

IParameterChanges* VST3Instance::prepareVST3ParameterChanges()
{
	// Drain the pending edits into the fixed-size change list. Runs on the
	// audio thread between blocks (or on the control thread during an idle
	// flush); allocation-free either way.
	vst3InputParameterChanges->clear();
	unsigned readIndex = vst3ParameterEditRead.load(memory_order_relaxed);
	unsigned writeIndex = vst3ParameterEditWrite.load(memory_order_acquire);
	while (readIndex != writeIndex)
	{
		const PendingVST3ParameterEdit& edit = vst3ParameterEditQueue[readIndex];
		vst3InputParameterChanges->add(edit.id, edit.value);
		readIndex = (readIndex + 1) % vst3ParameterEditQueueSize;
	}
	vst3ParameterEditRead.store(readIndex, memory_order_release);
	return vst3InputParameterChanges.get();
}

void VST3Instance::flushVST3ParameterChanges()
{
	// An idle processor consumes queued edits through one zero-sample process
	// call. A running processor drains them with its next audio block.
	ProcessData data;
	data.processMode = kRealtime;
	data.symbolicSampleSize = vst3SupportsDouble ? kSample64 : kSample32;
	data.numSamples = 0;
	data.numInputs = 0;
	data.numOutputs = 0;
	data.inputs = NULL;
	data.outputs = NULL;
	data.inputEvents = &emptyVST3EventList;
	vst3Lifecycle.flushParameters(data,
		[this]() {
			return vst3ParameterEditRead.load(memory_order_acquire)
				!= vst3ParameterEditWrite.load(memory_order_acquire);
		},
		[this]() { return prepareVST3ParameterChanges(); },
		data.symbolicSampleSize);
}

void VST3Instance::beginVST3EditorSession()
{
	vst3Lifecycle.beginEditorSession(vst3SupportsDouble ? kSample64 : kSample32);
}

void VST3Instance::endVST3EditorSession()
{
	vst3Lifecycle.endEditorSession();
	// Edits raised synchronously while leaving the session still need to reach
	// the processor; this call takes the one-shot idle path.
	flushVST3ParameterChanges();
}

bool VST3Instance::canProcessNow() const
{
	return vst3Lifecycle.canProcessNow();
}

void VST3Instance::prepareForProcessing(float sampleRate, int blockSize)
{
	if (vst3Processor == NULL || vst3Component == NULL)
		return;

	this->sampleRate = sampleRate;
	// The bus width was negotiated in initialize()/negotiateChannelCount()
	// and the host's buffer layout is already frozen to it. Renegotiating
	// here (e.g. to usedChannelCount, which can be a smaller remainder for
	// the last instance) could change the reported channel counts after
	// the buffers were sized, so only re-activate the buses.
	applyVST3BusActivation();
	ProcessSetup setup;
	setup.processMode = kRealtime;
	setup.symbolicSampleSize = vst3SupportsDouble ? kSample64 : kSample32;
	setup.maxSamplesPerBlock = blockSize;
	setup.sampleRate = sampleRate;
	vst3Lifecycle.setupProcessing(setup);
	vst3SamplePosition = 0;
}

void VST3Instance::startProcessing()
{
	vst3Lifecycle.startProcessing();
}

template<typename SampleType>
void VST3Instance::processVst3Replacing(SampleType** inputArray, SampleType** outputArray, int frameCount)
{
	if (vst3Processor == NULL)
		return;
	AudioBusBuffers inputBuffers;
	inputBuffers.numChannels = vst3InputChannelCount;
	Vst3SampleTraits<SampleType>::attach(inputBuffers, inputArray);
	AudioBusBuffers outputBuffers;
	outputBuffers.numChannels = vst3OutputChannelCount;
	Vst3SampleTraits<SampleType>::attach(outputBuffers, outputArray);
	ProcessData data;
	data.processMode = kRealtime;
	data.symbolicSampleSize = Vst3SampleTraits<SampleType>::symbolicSampleSize;
	data.numSamples = frameCount;
	data.numInputs = vst3InputBusCount > 0 ? 1 : 0;
	data.numOutputs = vst3OutputBusCount > 0 ? 1 : 0;
	data.inputs = data.numInputs > 0 ? &inputBuffers : NULL;
	data.outputs = data.numOutputs > 0 ? &outputBuffers : NULL;
	data.inputParameterChanges = prepareVST3ParameterChanges();
	data.inputEvents = &emptyVST3EventList;
	data.processContext = &vst3ProcessContext;
	vst3ProcessContext.state = ProcessContext::kPlaying | ProcessContext::kContTimeValid;
	vst3ProcessContext.sampleRate = sampleRate;
	vst3ProcessContext.projectTimeSamples = vst3SamplePosition;
	vst3ProcessContext.continousTimeSamples = vst3SamplePosition;
	if (vst3Processor->process(data) == kResultOk)
		vst3SamplePosition += frameCount;
}

void VST3Instance::processReplacing(float** inputArray, float** outputArray, int frameCount)
{
	processVst3Replacing(inputArray, outputArray, frameCount);
}

void VST3Instance::processDoubleReplacing(double** inputArray, double** outputArray, int frameCount)
{
	processVst3Replacing(inputArray, outputArray, frameCount);
}

void VST3Instance::process(float** inputArray, float** outputArray, int frameCount)
{
	// VST3 has no accumulating process call; the replacing one serves both.
	processVst3Replacing(inputArray, outputArray, frameCount);
}

void VST3Instance::stopProcessing()
{
	vst3Lifecycle.stopProcessing();
	// Edits that arrived during the audio run but after its last block
	// still need to reach the processor.
	flushVST3ParameterChanges();
}
