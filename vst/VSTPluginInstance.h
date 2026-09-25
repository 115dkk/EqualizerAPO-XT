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

// The public face of one hosted plug-in. The format-specific work sits behind
// it in VST2Instance or VST3Instance (VSTFormatInstance.h), chosen once when
// the instance is constructed; this header deliberately includes neither the
// VST2 nor the VST3 SDK headers, so its callers do not compile them.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "platform/windows/Win32Resource.h"
#include "VST3BusLayout.h"

class VSTFormatInstance;
class VSTPluginLibrary;

class VSTPluginInstance
{
public:
	VSTPluginInstance(const std::shared_ptr<VSTPluginLibrary>& library, int processLevel);
	~VSTPluginInstance();
	VSTPluginInstance(const VSTPluginInstance&) = delete;
	VSTPluginInstance& operator=(const VSTPluginInstance&) = delete;

	bool initialize();

	int numInputs() const;
	int numOutputs() const;
	// Proposes a bus layout matching channelCount to the plugin (VST3 only;
	// VST2 channel counts are fixed by the effect). Returns true when the
	// plugin afterwards spans at least channelCount channels, so a single
	// instance can process the whole device width. numInputs()/numOutputs()
	// reflect the negotiated result either way.
	bool negotiateChannelCount(int channelCount,
		const std::vector<std::wstring>& channelNames);
	// Proposes different input and output widths - a stereo input bus feeding
	// a full-width output bus, the DAW-style layout upmixer plugins key their
	// engine on. Returns true only when the plugin accepts both widths
	// exactly; on rejection the plugin's own preferred layout is re-applied.
	bool negotiateBusChannelCounts(int inputChannelCount, int outputChannelCount,
		const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames);
	// Negotiates the logical contract used by VSTPlugin Input/Output. Explicit directions
	// accept only arrangements belonging to that layout; Auto directions retain
	// the existing device-width negotiation and may use the plug-in's current
	// arrangement. No preferred-layout fallback is applied after a failure.
	bool negotiateBusLayouts(VST3BusLayout inputLayout, VST3BusLayout outputLayout,
		int automaticChannelCount, const std::vector<std::wstring>& inputChannelNames,
		const std::vector<std::wstring>& outputChannelNames);
	// Logical names for the VST3 arrangements most recently reported by the
	// processor. Unknown or vendor-specific arrangements intentionally remain
	// empty so diagnostics never claim a layout from channel count alone.
	std::optional<VST3BusLayout> getNegotiatedVST3InputLayout() const;
	std::optional<VST3BusLayout> getNegotiatedVST3OutputLayout() const;
	const std::vector<int>& getVST3InputChannelMapping() const;
	const std::vector<int>& getVST3OutputChannelMapping() const;
	bool canReplacing() const;
	bool isVST3() const;
	bool canProcessNow() const;
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

private:
	std::shared_ptr<VSTPluginLibrary> library;
	// Never null: VST2Instance or VST3Instance, fixed for the instance's life.
	std::unique_ptr<VSTFormatInstance> host;
};
