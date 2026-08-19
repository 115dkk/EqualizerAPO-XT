/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <array>
#include <atomic>
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>
#include "aeffectx.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"
#include "platform/windows/Win32Resource.h"
#include "VST3BusLayout.h"

class VSTPluginLibrary;

struct VST2EffectDeleter
{
	void operator()(vst_effect_t* effect) const noexcept;
};

using VST2EffectPtr = std::unique_ptr<vst_effect_t, VST2EffectDeleter>;

class VSTPluginInstance
{
public:
	VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
	~VSTPluginInstance();

	bool initialize();

	int numInputs() const;
	int numOutputs() const;
	// Proposes a bus layout matching channelCount to the plugin (VST3 only;
	// VST2 channel counts are fixed by the effect). Returns true when the
	// plugin afterwards spans at least channelCount channels, so a single
	// instance can process the whole device width. numInputs()/numOutputs()
	// reflect the negotiated result either way.
	bool negotiateChannelCount(int channelCount);
	// Proposes different input and output widths - a stereo input bus feeding
	// a full-width output bus, the DAW-style layout upmixer plugins key their
	// engine on. Returns true only when the plugin accepts both widths
	// exactly; on rejection the plugin's own preferred layout is re-applied.
	bool negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount);
	// Negotiates the logical contract used by VSTPlugin Input/Output. Explicit directions
	// accept only arrangements belonging to that layout; Auto directions retain
	// the existing device-width negotiation and may use the plug-in's current
	// arrangement. No preferred-layout fallback is applied after a failure.
	bool negotiateBusLayouts(VST3BusLayout inputLayout, VST3BusLayout outputLayout,
		int automaticChannelCount);
	// Logical names for the VST3 arrangements most recently reported by the
	// processor. Unknown or vendor-specific arrangements intentionally remain
	// empty so diagnostics never claim a layout from channel count alone.
	std::optional<VST3BusLayout> getNegotiatedVST3InputLayout() const;
	std::optional<VST3BusLayout> getNegotiatedVST3OutputLayout() const;
	// Supplies semantic EAPO channel names for the next VST3 negotiation.
	// The two-vector form supports asymmetric upmixer buses.
	void setChannelNameHints(const std::vector<std::wstring>& channelNames);
	void setBusChannelNameHints(const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames);
	const std::vector<int>& getVST3InputChannelMapping() const;
	const std::vector<int>& getVST3OutputChannelMapping() const;
	bool canReplacing() const;
	int uniqueID() const;
	std::wstring getName() const;
	int getUsedChannelCount() const;
	void setUsedChannelCount(int count);
	float getSampleRate() const;
	int getProcessLevel() const;
	void setProcessLevel(int value);
	int getLanguage() const;
	void setLanguage(int value);
	bool canDoubleReplacing() const;
	int getInitialDelay() const;

	void prepareForProcessing(float sampleRate, int blockSize);
	void writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap);
	void readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const;

	void startProcessing();
	void processDoubleReplacing(double** inputArray, double** outputArray, int frameCount);
	// Shared VST3 body behind both widths; defined in VSTPluginInstance.cpp.
	template<typename SampleType>
	void processVst3Replacing(SampleType** inputArray, SampleType** outputArray, int frameCount);

	void processReplacing(float** inputArray, float** outputArray, int frameCount);
	void process(float** inputArray, float** outputArray, int frameCount);
	void stopProcessing();
	void stopProcessingSafely() noexcept;

	bool startEditing(HWND hWnd, short* width, short* height, double scaleFactor = 1.0);
	void doIdle();
	void stopEditing();

	void setAutomateFunc(std::function<void()> func);
	void onAutomate();

	void setSizeWindowFunc(std::function<void(int, int)> func);
	void onSizeWindow(int w, int h);
	// Backing store for VST_HOST_OPCODE_GET_TIME, refreshed and returned per
	// call. Per instance: plugins in different audio streams process
	// concurrently, so a shared global here would let them race on one struct.
	vst_time_info* hostTimeInfo();

private:
	class VST3HostContext;
	class VST3MemoryStream;
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
	static constexpr int vst3MaxArrangementCandidates = 4;

	// Audit #250 F040: the VST2 loader distinguishes its failure modes so
	// initialize() can log the actual reason (the old bool collapsed every
	// failure into "an exception").
	enum class Vst2LoadResult
	{
		Loaded,
		Crashed,
		NoEntryPoint,
		WrongMagicNumber
	};

	Vst2LoadResult initializeVST2();
	bool initializeVST3();
	void releaseVST3();
	void configureVST3Buses(int requestedChannelCount);
	void configureVST3Buses(int requestedInputChannelCount, int requestedOutputChannelCount);
	void applyVST3BusActivation();
	static int semanticSpeakerArrangementCandidatesForChannelNames(const std::vector<std::wstring>& channelNames,
		Steinberg::Vst::SpeakerArrangement* candidates);
	static int speakerArrangementCandidatesForChannelCount(int count, const std::vector<std::wstring>& channelNames,
		Steinberg::Vst::SpeakerArrangement* candidates);
	static int speakerArrangementCandidatesForLayout(VST3BusLayout layout, int automaticChannelCount,
		const std::vector<std::wstring>& channelNames, Steinberg::Vst::SpeakerArrangement currentArrangement,
		Steinberg::Vst::SpeakerArrangement* candidates);
	static bool arrangementMatchesLayout(Steinberg::Vst::SpeakerArrangement arrangement,
		VST3BusLayout layout);
	bool acceptedVST3BusMetadataIsConsistent() const;
	bool refreshAcceptedVST3Arrangements();
	void updateVST3ChannelMappings();
	static bool buildVST3ChannelMapping(Steinberg::Vst::SpeakerArrangement arrangement,
		const std::vector<std::wstring>& channelNames, std::vector<int>& mapping);
	int vst3BusChannelCount(Steinberg::Vst::BusDirection direction) const;
	void onVST3ParameterEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);
	void queueVST3ParameterEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);
	Steinberg::Vst::IParameterChanges* prepareVST3ParameterChanges();
	void flushVST3ParameterChanges();
	void beginVST3EditorSession();
	void endVST3EditorSession();

	std::shared_ptr<VSTPluginLibrary> library;
	VST2EffectPtr effect;
	Steinberg::IPtr<Steinberg::Vst::IComponent> vst3Component;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> vst3Processor;
	Steinberg::IPtr<Steinberg::Vst::IEditController> vst3Controller;
	Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> vst3ComponentConnection;
	Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> vst3ControllerConnection;
	Steinberg::IPtr<Steinberg::IPlugView> vst3View;
	bool vst3ViewAttached = false;
	winutil::UniqueWindowHandle vst3EditorHostWindow;
	Steinberg::IPtr<VST3HostContext> vst3HostContext;
	std::unique_ptr<VST3ParameterChanges> vst3InputParameterChanges;
	std::array<PendingVST3ParameterEdit, vst3ParameterEditQueueSize> vst3ParameterEditQueue{};
	std::atomic<unsigned> vst3ParameterEditWrite{ 0 };
	std::atomic<unsigned> vst3ParameterEditRead{ 0 };
	std::mutex vst3LifecycleMutex;
	std::atomic<bool> vst3ParameterFlushInProgress{ false };
	int vst3InputBusCount = 0;
	int vst3OutputBusCount = 0;
	int vst3InputChannelCount = 0;
	int vst3OutputChannelCount = 0;
	Steinberg::Vst::SpeakerArrangement vst3InputArrangement = Steinberg::Vst::SpeakerArr::kEmpty;
	Steinberg::Vst::SpeakerArrangement vst3OutputArrangement = Steinberg::Vst::SpeakerArr::kEmpty;
	std::vector<std::wstring> vst3InputChannelNameHints;
	std::vector<std::wstring> vst3OutputChannelNameHints;
	std::vector<int> vst3InputChannelMapping;
	std::vector<int> vst3OutputChannelMapping;
	bool vst3SupportsDouble = false;
	// Whether initialize() succeeded on the component / whether the
	// controller is a separately created object. A single-component plug-in
	// exposes IEditController from the already initialized component object,
	// which must not be initialized or terminated a second time.
	bool vst3ComponentInitialized = false;
	bool vst3ControllerInitializedSeparately = false;
	bool vst3Active = false;
	// An open editor view holds the processor in the Processing state for the
	// whole session, so parameter-flush process calls need no per-edit
	// setActive/setProcessing cycling. Guarded by vst3LifecycleMutex.
	bool vst3EditorSession = false;
	std::atomic<bool> vst3Processing{ false };
	Steinberg::Vst::ProcessContext vst3ProcessContext = {};
	Steinberg::Vst::TSamples vst3SamplePosition = 0;
	std::function<void()> automateFunc;
	std::function<void(int, int)> sizeWindowFunc;
	double editorScaleFactor = 1.0;
	float sampleRate = 0.0f;
	int usedChannelCount = -1;
	int processLevel = 0;
	int language = 1;
	vst_time_info vstTime{ 0,0,0,0,0,0,0,0,0,0,{0}, 0xFFFF };
};
