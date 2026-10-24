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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/windows/Win32Resource.h"
#include "VST3BusLayout.h"

class VSTPluginLibrary;

class VSTPluginInstance
{
public:
	// Picks the format once. This header deliberately includes no VST SDK.
	static std::unique_ptr<VSTPluginInstance> create(
		const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
	bool isVST3() const;

	virtual ~VSTPluginInstance() = default;
	VSTPluginInstance(const VSTPluginInstance&) = delete;
	VSTPluginInstance& operator=(const VSTPluginInstance&) = delete;

	virtual bool initialize() = 0;

	virtual int numInputs() const = 0;
	virtual int numOutputs() const = 0;
	// Proposes a bus layout matching channelCount to the plugin (VST3 only;
	// VST2 channel counts are fixed by the effect). Returns true when the
	// plugin afterwards spans at least channelCount channels, so a single
	// instance can process the whole device width. numInputs()/numOutputs()
	// reflect the negotiated result either way.
	virtual bool negotiateChannelCount(int channelCount,
		const std::vector<std::wstring>& channelNames) = 0;
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
	// Logical names for the VST3 arrangements most recently reported by the
	// processor. Unknown or vendor-specific arrangements intentionally remain
	// empty so diagnostics never claim a layout from channel count alone.
	virtual std::optional<VST3BusLayout> getNegotiatedVST3InputLayout() const = 0;
	virtual std::optional<VST3BusLayout> getNegotiatedVST3OutputLayout() const = 0;
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

	bool startEditing(HWND hWnd, short* width, short* height, double scaleFactor = 1.0);
	// Re-scales an open editor after its host frame moves to a display
	// with another device pixel ratio. Returns false when no editor is open.
	bool setEditorScale(double scaleFactor);
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
	const std::vector<int>& getVST3InputChannelMapping() const { return inputMapping; }
	const std::vector<int>& getVST3OutputChannelMapping() const { return outputMapping; }

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

	VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel)
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
