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

#include <cstdint>
#include <functional>
#include <malloc.h>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "platform/windows/Win32Resource.h"
#include "dsp/MxcsrGuard.h"
#include "VST3BusLayout.h"
#include "runtime/memory/AlignedMemory.h"
#include "platform/windows/StackGuarantee.h"

class VSTPluginLibrary;

class VSTPluginInstance
{
public:
	enum class ProcessLevel { User = 1, Realtime = 2 };
	// Picks the format once. This header deliberately includes no VST SDK.
	static std::unique_ptr<VSTPluginInstance> create(
		const std::shared_ptr<VSTPluginLibrary>& library, VSTPluginInstance::ProcessLevel processLevel);
	bool isVST3() const;

	virtual ~VSTPluginInstance() = default;
	VSTPluginInstance(const VSTPluginInstance&) = delete;
	VSTPluginInstance& operator=(const VSTPluginInstance&) = delete;

	enum class InitializationFailure
	{
		Refused,
		StructuredException
	};
	virtual bool initialize() = 0;
	virtual InitializationFailure initializationFailure() const {return InitializationFailure::Refused;}

	virtual int numInputs() const = 0;
	virtual int numOutputs() const = 0;
private:
	// Proposes a bus layout matching channelCount to the plugin (VST3 only;
	// VST2 channel counts are fixed by the effect). Returns true when the
	// plugin afterwards spans at least channelCount channels, so a single
	// instance can process the whole device width. numInputs()/numOutputs()
	// reflect the negotiated result either way.
	virtual bool negotiateChannelCount(int channelCount,
		const std::vector<std::wstring>& channelNames) = 0;
protected:
	// Proposes different input and output widths - a stereo input bus feeding
	// a full-width output bus, the DAW-style layout upmixer plugins key their
	// engine on. Returns true only when the plugin accepts both widths
	// exactly; on rejection the plugin's own preferred layout is re-applied.
	virtual bool negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount,
		const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames) = 0;
	// Negotiates the logical contract used by VSTPlugin Input/Output. Explicit directions
	// accept only arrangements belonging to that layout; Auto directions retain
	// the existing device-width negotiation and may use the plug-in's current
	// arrangement. No preferred-layout fallback is applied after a failure.
	virtual bool negotiateBusLayouts(VST3BusLayout inputLayout, VST3BusLayout outputLayout,
		int automaticChannelCount, const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames) = 0;
public:
	// Logical names for the VST3 arrangements most recently reported by the
	// processor. Unknown or vendor-specific arrangements intentionally remain
	// empty so diagnostics never claim a layout from channel count alone.
	virtual std::optional<VST3BusLayout> getNegotiatedVST3InputLayout() const = 0;
	virtual std::optional<VST3BusLayout> getNegotiatedVST3OutputLayout() const = 0;
	virtual bool canProcessNow() const = 0;
	virtual int uniqueID() const = 0;
	virtual std::wstring getName() const = 0;
	virtual int getInitialDelay() const = 0;

	struct NegotiationRequest
	{
		std::optional<VST3BusContract> contract;
		int channelCount = 2;
		std::vector<std::wstring> channelNames;
		bool stereoInput = false;
	};
	struct NegotiationResult
	{
		enum class Path { Automatic, Preferred, StereoInput, Contract };
		bool accepted = false;
		int inputs = 0;
		int outputs = 0;
		std::optional<VST3BusLayout> inputLayout;
		std::optional<VST3BusLayout> outputLayout;
		Path path = Path::Automatic;
		std::wstring reason;
	};
	NegotiationResult negotiate(const NegotiationRequest& request);

	// Serial split instances share one workspace, as the engine did before
	// moving conversion here. Concurrent instances must not share it.
	struct ProcessingWorkspace
	{
		std::vector<float*> floatInputs, floatOutputs;
		AlignedMemory::UniqueAllocation<float> floatInputBuffer, floatOutputBuffer;
		std::vector<double*> doubleInputs, doubleOutputs;
		AlignedMemory::UniqueAllocation<double> doubleInputBuffer, doubleOutputBuffer;
		int inputs = 0, outputs = 0, frames = 0;
	};
	enum class InputWidth { Double, Float };
	bool prepareForProcessing(float sampleRate, int blockSize,
		InputWidth width = InputWidth::Double,
		const std::shared_ptr<ProcessingWorkspace>& sharedWorkspace = nullptr);
	void processBlock(double** input, double** output, unsigned frames);
	void processBlock(float** input, float** output, unsigned frames);

	class ProcessingLease
	{
	public:
		ProcessingLease() = default;
		~ProcessingLease() { reset(); }
		ProcessingLease(const ProcessingLease&) = delete;
		ProcessingLease& operator=(const ProcessingLease&) = delete;
		bool acquire(VSTPluginInstance* instance);
		void reset();
	private:
		VSTPluginInstance* owner = nullptr;
	};
	struct StateRestoreResult
	{
		const wchar_t* reason = nullptr;
		explicit operator bool() const { return reason == nullptr; }
	};
	virtual StateRestoreResult writeToEffect(const std::wstring& chunkData,
		const std::unordered_map<std::wstring, float>& paramMap) = 0;
	virtual void readFromEffect(std::wstring& chunkData,
		std::unordered_map<std::wstring, float>& paramMap) const = 0;
	using WritableParameterValues = std::vector<std::pair<uint32_t, double>>;
	// VST3-only semantic state used by the Editor to set aside volatile raw
	// state and read-only meters. VST2 keeps its existing chunk comparison.
	virtual bool readWritableParameterValues(WritableParameterValues& values) const
	{
		values.clear();
		return false;
	}
	virtual uint64_t parameterEditSequence() const { return 0; }

	virtual bool startProcessing() = 0;
private:
	virtual void processDoubleReplacing(double** inputArray, double** outputArray, int frameCount) = 0;
	virtual void processReplacing(float** inputArray, float** outputArray, int frameCount) = 0;
	virtual void process(float** inputArray, float** outputArray, int frameCount) = 0;
public:
	virtual void stopProcessing() = 0;
	void stopProcessingSafely() noexcept
	{
		const unsigned __int64 fpControlState = captureFpControlState();
		ensurePluginCallStackGuarantee();
		volatile bool stackOverflow = false;
		__try
		{
			stopProcessing();
			restoreFpControlState(fpControlState);
		}
		__except ((stackOverflow = GetExceptionCode() == EXCEPTION_STACK_OVERFLOW)
			? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_EXECUTE_HANDLER)
		{
			restoreFpControlState(fpControlState);
			// Cleanup is best effort across a third-party native-code boundary.
		}
		// A caught overflow consumes the thread's guard page; re-arm it after SEH unwinds.
		if (stackOverflow)
			_resetstkoflw();
	}

	bool startEditing(HWND hWnd, short* width, short* height, double scaleFactor = 1.0);
	// Re-scales an open editor after its host frame moves to a display
	// with another device pixel ratio. Returns false when no editor is open.
	bool setEditorScale(double scaleFactor);
	virtual void doIdle() = 0;
	virtual void stopEditing() = 0;
	// A size request from the plug-in's editor, in the units its format uses.
	virtual void onSizeWindow(int w, int h) = 0;

private:
	// The sample widths the plug-in takes, recorded by the implementation
	// when it loads (and, for VST2, refreshed before processing starts).
	bool canReplacing() const { return floatProcessing; }
	bool canDoubleReplacing() const { return doubleProcessing; }
public:
	// EAPO slot -> plug-in bus slot. Empty for VST2, which keeps the
	// identity order.
	const std::vector<int>& getVST3InputChannelMapping() const { return inputMapping; }
	const std::vector<int>& getVST3OutputChannelMapping() const { return outputMapping; }

	int getUsedChannelCount() const { return usedChannelCount; }
	void setUsedChannelCount(int count) { usedChannelCount = count; }
	float getSampleRate() const { return sampleRate; }
	ProcessLevel getProcessLevel() const { return processLevel; }
	int getLanguage() const { return language; }
	void setLanguage(int value) { language = value; }

	void setAutomateFunc(std::function<void()> func) { automateFunc = std::move(func); }
	void onAutomate()
	{
		if (automateFunc)
			automateFunc();
	}
	void setSizeWindowFunc(std::function<void(int, int)> func) { sizeWindowFunc = std::move(func); }

protected:
	virtual bool prepareProcessingImpl(float sampleRate, int blockSize) = 0;
	// Derived destructors call this before releasing any plug-in object; the
	// base destructor would be too late to suppress teardown callbacks.
	void clearCallbacks() noexcept
	{
		automateFunc = nullptr;
		sizeWindowFunc = nullptr;
	}
	static constexpr int defaultEditorWidth = 400;
	static constexpr int defaultEditorHeight = 300;
	static short logicalEditorSize(int physical, double scaleFactor);
	virtual bool startEditingImpl(HWND hWnd, short* width, short* height, double scaleFactor) = 0;
	virtual bool rescaleEditorImpl(double scaleFactor) = 0;

	VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, VSTPluginInstance::ProcessLevel processLevel)
		: library(library), processLevel(processLevel)
	{
	}

	// Hands a logical editor size to the Qt frame that hosts the editor.
	void notifySizeWindow(int w, int h)
	{
		if (sizeWindowFunc)
			sizeWindowFunc(w, h);
	}

	std::shared_ptr<VSTPluginLibrary> library;
	float sampleRate = 0.0f;
	int usedChannelCount = -1;
	bool floatProcessing = true;
	bool doubleProcessing = false;
	std::vector<int> inputMapping;
	std::vector<int> outputMapping;
	std::function<void()> automateFunc;
	std::function<void(int, int)> sizeWindowFunc;

private:
	bool metadataValid() const;
	std::shared_ptr<ProcessingWorkspace> workspace;
	int processingInputs = 0;
	int processingOutputs = 0;
	unsigned processingFrames = 0;
	ProcessLevel processLevel = ProcessLevel::User;
	int language = 1;
};
