/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The format-specific side of VSTPluginInstance (audit #348 C4/TD-40). The
	facade picks one implementation when it is constructed - VST2Instance or
	VST3Instance, from the loaded library's ABI - and forwards every call to
	it, so no instance method asks which format it is hosting.

	The state both formats share lives here rather than in the facade: the
	plug-ins' host callbacks (the VST2 audioMaster, the VST3 host context)
	reach the implementation directly and read it without a pointer back to
	the facade. The per-block queries the engine makes before each process
	call (sample width, channel mapping) read plain members, so the only
	indirect call a block adds is the process call itself.

	Not part of the public API: only VSTPluginInstance.cpp and the two
	implementations include it.
*/

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/windows/Win32Resource.h"
#include "VST3BusLayout.h"

class VSTPluginLibrary;

class VSTFormatInstance
{
public:
	virtual ~VSTFormatInstance() = default;
	VSTFormatInstance(const VSTFormatInstance&) = delete;
	VSTFormatInstance& operator=(const VSTFormatInstance&) = delete;

	virtual bool initialize() = 0;

	virtual int numInputs() const = 0;
	virtual int numOutputs() const = 0;
	virtual bool negotiateChannelCount(int channelCount,
		const std::vector<std::wstring>& channelNames) = 0;
	virtual bool negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount,
		const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames) = 0;
	virtual bool negotiateBusLayouts(VST3BusLayout inputLayout, VST3BusLayout outputLayout,
		int automaticChannelCount, const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames) = 0;
	virtual std::optional<VST3BusLayout> negotiatedInputLayout() const = 0;
	virtual std::optional<VST3BusLayout> negotiatedOutputLayout() const = 0;
	virtual bool canProcessNow() const = 0;
	virtual int uniqueID() const = 0;
	virtual std::wstring getName() const = 0;
	virtual int getInitialDelay() const = 0;

	virtual void prepareForProcessing(float sampleRate, int blockSize) = 0;
	virtual void writeToEffect(const std::wstring& chunkData,
		const std::unordered_map<std::wstring, float>& paramMap) = 0;
	virtual void readFromEffect(std::wstring& chunkData,
		std::unordered_map<std::wstring, float>& paramMap) const = 0;

	virtual void startProcessing() = 0;
	virtual void processDoubleReplacing(double** inputArray, double** outputArray, int frameCount) = 0;
	virtual void processReplacing(float** inputArray, float** outputArray, int frameCount) = 0;
	virtual void process(float** inputArray, float** outputArray, int frameCount) = 0;
	virtual void stopProcessing() = 0;
	void stopProcessingSafely() noexcept
	{
		__try
		{
			stopProcessing();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// Cleanup is best effort across a third-party native-code boundary.
		}
	}

	// scaleFactor is already normalized (> 0) and *width/*height already hold
	// the logical default size when these are called.
	virtual bool startEditing(HWND hWnd, short* width, short* height, double scaleFactor) = 0;
	virtual void doIdle() = 0;
	virtual void stopEditing() = 0;
	// A size request from the plug-in's editor, in the units its format uses.
	virtual void onSizeWindow(int w, int h) = 0;

	// The sample widths the plug-in takes, recorded by the implementation
	// when it loads (and, for VST2, refreshed before processing starts).
	bool canReplacing() const { return floatProcessing; }
	bool canDoubleReplacing() const { return doubleProcessing; }
	// EAPO slot -> plug-in bus slot. Empty for VST2, which keeps the
	// identity order.
	const std::vector<int>& inputChannelMapping() const { return inputMapping; }
	const std::vector<int>& outputChannelMapping() const { return outputMapping; }

	int getUsedChannelCount() const { return usedChannelCount; }
	void setUsedChannelCount(int count) { usedChannelCount = count; }
	float getSampleRate() const { return sampleRate; }
	int getProcessLevel() const { return processLevel; }
	void setProcessLevel(int value) { processLevel = value; }
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
	VSTFormatInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
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
	int processLevel = 0;
	int language = 1;
};

// The two implementations, each defined in its own translation unit so the
// VST2 and VST3 SDK headers never meet in one (the VST3 SDK's VST_VERSION
// macro collides with an enumerator of the same name in aeffectx.h).
std::unique_ptr<VSTFormatInstance> createVST2Instance(
	const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
std::unique_ptr<VSTFormatInstance> createVST3Instance(
	const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
