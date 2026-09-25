/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	VST3 hosting behind VSTPluginInstance (audit #348 C4/TD-40). One object
	owns the component, processor and controller of one plug-in instance, the
	bus negotiation, the lifecycle (VST3Lifecycle) and the parameter-edit ring
	that carries GUI edits to the processor.

	Its translation units:
	  - VST3Instance.cpp            : initialize/release, bus negotiation and
	                                  the format queries.
	  - VST3Instance.Processing.cpp : the parameter-edit ring, the lifecycle
	                                  calls and the process group.
	  - VST3Instance.Editor.cpp     : the IPlugView editor window.
	  - VST3Instance.State.cpp      : component/controller state read/write.
*/

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "platform/windows/Win32Resource.h"
#include "VSTFormatInstance.h"
#include "VST3Lifecycle.h"

class VST3HostContext;

class VST3Instance final : public VSTFormatInstance
{
public:
	VST3Instance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
	~VST3Instance() override;

	bool initialize() override;

	int numInputs() const override;
	int numOutputs() const override;
	bool negotiateChannelCount(int channelCount,
		const std::vector<std::wstring>& channelNames) override;
	bool negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount,
		const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames) override;
	bool negotiateBusLayouts(VST3BusLayout inputLayout, VST3BusLayout outputLayout,
		int automaticChannelCount, const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames) override;
	std::optional<VST3BusLayout> negotiatedInputLayout() const override;
	std::optional<VST3BusLayout> negotiatedOutputLayout() const override;
	bool canProcessNow() const override;
	int uniqueID() const override;
	std::wstring getName() const override;
	int getInitialDelay() const override;

	void prepareForProcessing(float sampleRate, int blockSize) override;
	void writeToEffect(const std::wstring& chunkData,
		const std::unordered_map<std::wstring, float>& paramMap) override;
	void readFromEffect(std::wstring& chunkData,
		std::unordered_map<std::wstring, float>& paramMap) const override;

	void startProcessing() override;
	void processDoubleReplacing(double** inputArray, double** outputArray, int frameCount) override;
	void processReplacing(float** inputArray, float** outputArray, int frameCount) override;
	void process(float** inputArray, float** outputArray, int frameCount) override;
	void stopProcessing() override;

	bool startEditing(HWND hWnd, short* width, short* height, double scaleFactor) override;
	void doIdle() override;
	void stopEditing() override;
	// w/h arrive in physical pixels from the view's resizeView request.
	void onSizeWindow(int w, int h) override;

	// A performEdit from the plug-in's controller, relayed by VST3HostContext.
	void onVST3ParameterEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

private:
	class VST3ParameterChanges;

	// One GUI parameter edit waiting for the processor. The ring below is
	// written by the edit path and drained by whoever runs the next process
	// call; see queueVST3ParameterEdit for the threading contract.
	struct PendingVST3ParameterEdit
	{
		Steinberg::Vst::ParamID id = 0;
		Steinberg::Vst::ParamValue value = 0.0;
	};

	static constexpr unsigned vst3ParameterEditQueueSize = 1024;

	void releaseVST3();
	void configureVST3Buses(int requestedChannelCount,
		const std::vector<std::wstring>& channelNames);
	void configureVST3Buses(int requestedInputChannelCount, int requestedOutputChannelCount,
		const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames);
	void applyVST3BusActivation();
	bool acceptedVST3BusMetadataIsConsistent() const;
	bool refreshAcceptedVST3Arrangements();
	void updateVST3ChannelMappings();
	int vst3BusChannelCount(Steinberg::Vst::BusDirection direction) const;
	bool queueVST3ParameterEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);
	bool queueRestoredParameterEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);
	// Logs the values a state restore could not queue; see
	// queueRestoredParameterEdit.
	void reportDroppedRestoredParameters(unsigned dropped) const;
	Steinberg::Vst::IParameterChanges* prepareVST3ParameterChanges();
	void flushVST3ParameterChanges();
	void beginVST3EditorSession();
	void endVST3EditorSession();
	// The one process-call choreography behind both sample widths; defined
	// (and instantiated) in VST3Instance.Processing.cpp only.
	template<typename SampleType>
	void processVst3Replacing(SampleType** inputArray, SampleType** outputArray, int frameCount);

	Steinberg::IPtr<Steinberg::Vst::IComponent> vst3Component;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> vst3Processor;
	Steinberg::IPtr<Steinberg::Vst::IEditController> vst3Controller;
	Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> vst3ComponentConnection;
	Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> vst3ControllerConnection;
	Steinberg::IPtr<Steinberg::IPlugView> vst3View;
	bool vst3ViewAttached = false;
	winutil::UniqueWindowHandle vst3EditorHostWindow;
	double editorScaleFactor = 1.0;
	Steinberg::IPtr<VST3HostContext> vst3HostContext;
	std::unique_ptr<VST3ParameterChanges> vst3InputParameterChanges;
	std::array<PendingVST3ParameterEdit, vst3ParameterEditQueueSize> vst3ParameterEditQueue{};
	std::atomic<unsigned> vst3ParameterEditWrite{ 0 };
	std::atomic<unsigned> vst3ParameterEditRead{ 0 };
	VST3Lifecycle vst3Lifecycle;
	int vst3InputBusCount = 0;
	int vst3OutputBusCount = 0;
	int vst3InputChannelCount = 0;
	int vst3OutputChannelCount = 0;
	Steinberg::Vst::SpeakerArrangement vst3InputArrangement = Steinberg::Vst::SpeakerArr::kEmpty;
	Steinberg::Vst::SpeakerArrangement vst3OutputArrangement = Steinberg::Vst::SpeakerArr::kEmpty;
	std::vector<std::wstring> vst3InputChannelNameHints;
	std::vector<std::wstring> vst3OutputChannelNameHints;
	bool vst3SupportsDouble = false;
	// Whether initialize() succeeded on the component / whether the
	// controller is a separately created object. A single-component plug-in
	// exposes IEditController from the already initialized component object,
	// which must not be initialized or terminated a second time.
	bool vst3ComponentInitialized = false;
	bool vst3ControllerInitializedSeparately = false;
	Steinberg::Vst::ProcessContext vst3ProcessContext = {};
	Steinberg::Vst::TSamples vst3SamplePosition = 0;
};
