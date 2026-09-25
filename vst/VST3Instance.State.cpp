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

#include "stdafx.h"
#include "VSTPluginLibrary.h"
#include "VST3Instance.h"
#include "VST3MemoryStream.h"
#include "VSTChunkBase64.h"
#include "services/logging/Logging.h"
#include "pluginterfaces/base/smartpointer.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;
using vstchunk::decodeBase64;
using vstchunk::encodeBase64;

namespace
{
	// Audit #250 F047: VST3's String128 carries no termination promise (the
	// VST2 side already force-terminates its buffers). Bound the read.
	wstring fromString128(const String128& text)
	{
		const wchar_t* characters = (const wchar_t*)text;
		size_t length = 0;
		while (length < 128 && characters[length] != L'\0')
			++length;
		return wstring(characters, length);
	}

	// A VST3 chunk used to carry the component state alone, losing the
	// controller-only state (UI preferences) and the parameter snapshot on
	// every save. The combined layout wraps all three behind a magic header;
	// a blob without the header is read as a bare component state, so
	// configurations written by older versions keep loading (the reverse
	// direction does not: an older build feeds the combined blob straight
	// into the component). Ported from the ripDZL fork's VST3 compatibility
	// work (github.com/ripDZL/EqualizerAPO-XT/pull/1).
	constexpr Steinberg::uint32 vst3HostStateMagic = 0x54533345; // "E3ST" little-endian
	constexpr Steinberg::uint32 vst3HostStateVersion = 1;

#pragma pack(push, 1)
	struct VST3HostStateHeader
	{
		Steinberg::uint32 magic = vst3HostStateMagic;
		Steinberg::uint32 version = vst3HostStateVersion;
		Steinberg::uint32 componentSize = 0;
		Steinberg::uint32 controllerSize = 0;
		Steinberg::uint32 parameterCount = 0;
	};

	struct VST3HostParameterState
	{
		ParamID id = 0;
		ParamValue value = 0.0;
	};
#pragma pack(pop)

	vector<char> combineVST3State(const vector<char>& component, const vector<char>& controller,
		const vector<VST3HostParameterState>& parameters)
	{
		// No controller-private state? Keep the bare legacy layout, which
		// older builds can also read. The parameter snapshot only rides along
		// with a controller blob: a component-only plug-in restores its
		// parameters from the component state itself.
		if (controller.empty())
			return component;
		if (component.size() > UINT32_MAX || controller.size() > UINT32_MAX || parameters.size() > UINT32_MAX)
			return component;
		const size_t parameterBytes = parameters.size() * sizeof(VST3HostParameterState);
		if (component.size() > SIZE_MAX - sizeof(VST3HostStateHeader) - controller.size()
			|| component.size() + sizeof(VST3HostStateHeader) + controller.size() > SIZE_MAX - parameterBytes)
			return component;
		VST3HostStateHeader header;
		header.componentSize = (Steinberg::uint32)component.size();
		header.controllerSize = (Steinberg::uint32)controller.size();
		header.parameterCount = (Steinberg::uint32)parameters.size();
		vector<char> combined(sizeof(header) + component.size() + controller.size() + parameterBytes);
		memcpy(combined.data(), &header, sizeof(header));
		if (!component.empty())
			memcpy(combined.data() + sizeof(header), component.data(), component.size());
		if (!controller.empty())
			memcpy(combined.data() + sizeof(header) + component.size(), controller.data(), controller.size());
		if (!parameters.empty())
			memcpy(combined.data() + sizeof(header) + component.size() + controller.size(),
				parameters.data(), parameterBytes);
		return combined;
	}

	void splitVST3State(const vector<char>& combined, vector<char>& component, vector<char>& controller,
		vector<VST3HostParameterState>& parameters)
	{
		// Default: the whole blob is a legacy bare component state.
		component = combined;
		controller.clear();
		parameters.clear();
		if (combined.size() < sizeof(VST3HostStateHeader))
			return;
		VST3HostStateHeader header;
		memcpy(&header, combined.data(), sizeof(header));
		const Steinberg::uint64 parameterBytes = (Steinberg::uint64)header.parameterCount * sizeof(VST3HostParameterState);
		const Steinberg::uint64 payloadSize = (Steinberg::uint64)header.componentSize + header.controllerSize + parameterBytes;
		if (header.magic != vst3HostStateMagic || header.version != vst3HostStateVersion
			|| payloadSize != (Steinberg::uint64)(combined.size() - sizeof(header)))
			return;
		component.assign(combined.begin() + sizeof(header), combined.begin() + sizeof(header) + header.componentSize);
		const size_t controllerStart = sizeof(VST3HostStateHeader) + header.componentSize;
		controller.assign(combined.begin() + controllerStart, combined.begin() + controllerStart + header.controllerSize);
		parameters.resize(header.parameterCount);
		if (!parameters.empty())
			memcpy(parameters.data(), combined.data() + controllerStart + header.controllerSize, (size_t)parameterBytes);
	}
}

void VST3Instance::writeToEffect(const std::wstring& chunkData,
	const std::unordered_map<std::wstring, float>& paramMap)
{
	if (chunkData != L"")
	{
		vector<char> data;
		if (decodeBase64(chunkData, data) && !data.empty())
		{
			vector<char> componentState;
			vector<char> controllerState;
			vector<VST3HostParameterState> parameters;
			splitVST3State(data, componentState, controllerState, parameters);
			if (!componentState.empty())
			{
				auto stream = IPtr<VST3MemoryStream>::adopt(new VST3MemoryStream(componentState));
				if (vst3Component != NULL)
					vst3Component->setState(stream.get());
				// The controller mirrors the component's state through
				// setComponentState; setState is reserved for its own
				// (controller-only) blob below.
				stream->seek(0, IBStream::kIBSeekSet);
				if (vst3Controller != NULL)
					vst3Controller->setComponentState(stream.get());
			}
			if (vst3Controller != NULL && !controllerState.empty())
			{
				auto stream = IPtr<VST3MemoryStream>::adopt(new VST3MemoryStream(controllerState));
				vst3Controller->setState(stream.get());
			}
			// The saved parameter snapshot drives both sides: the
			// controller for the GUI and the processor via the queue
			// (some plug-ins only apply GUI-visible values from
			// parameter changes, not from setState).
			bool parameterQueued = false;
			unsigned dropped = 0;
			for (const VST3HostParameterState& parameter : parameters)
			{
				if (vst3Controller != NULL)
					vst3Controller->setParamNormalized(parameter.id, parameter.value);
				if (!queueRestoredParameterEdit(parameter.id, parameter.value))
					dropped++;
				parameterQueued = true;
			}
			if (parameterQueued)
				flushVST3ParameterChanges();
			reportDroppedRestoredParameters(dropped);
		}
	}
	else if (vst3Controller != NULL)
	{
		bool parameterQueued = false;
		unsigned dropped = 0;
		for (const auto& it : paramMap)
		{
			for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
			{
				ParameterInfo info;
				if (vst3Controller->getParameterInfo(i, info) == kResultOk
					&& it.first == fromString128(info.title))
				{
					vst3Controller->setParamNormalized(info.id, it.second);
					if (!queueRestoredParameterEdit(info.id, it.second))
						dropped++;
					parameterQueued = true;
					break;
				}
			}
		}
		if (parameterQueued)
			flushVST3ParameterChanges();
		reportDroppedRestoredParameters(dropped);
	}
}

void VST3Instance::readFromEffect(std::wstring& chunkData,
	std::unordered_map<std::wstring, float>& paramMap) const
{
	chunkData = L"";
	paramMap.clear();

	const auto readPluginState = [](auto* plugin) {
		vector<char> data;
		if (plugin != NULL)
		{
			auto stream = IPtr<VST3MemoryStream>::adopt(new VST3MemoryStream());
			if (plugin->getState(stream.get()) == kResultOk)
				data = stream->getData();
		}
		return data;
	};
	const vector<char> componentState = readPluginState(vst3Component.get());
	// A single-component plug-in's controller getState IS the component
	// getState; storing it twice would double the chunk for nothing.
	const vector<char> controllerState = vst3ControllerInitializedSeparately
		? readPluginState(vst3Controller.get()) : vector<char>();
	vector<VST3HostParameterState> parameters;
	if (vst3Controller != NULL)
	{
		for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
		{
			ParameterInfo info;
			if (vst3Controller->getParameterInfo(i, info) == kResultOk)
				parameters.push_back({ info.id, vst3Controller->getParamNormalized(info.id) });
		}
	}
	if (!componentState.empty() || !controllerState.empty())
	{
		const vector<char> combined = combineVST3State(componentState, controllerState, parameters);
		encodeBase64(combined.data(), combined.size(), chunkData);
	}

	if (chunkData == L"" && vst3Controller != NULL)
	{
		for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
		{
			ParameterInfo info;
			if (vst3Controller->getParameterInfo(i, info) == kResultOk)
				paramMap[fromString128(info.title)] = (float)vst3Controller->getParamNormalized(info.id);
		}
	}
}

void VST3Instance::reportDroppedRestoredParameters(unsigned dropped) const
{
	if (dropped == 0)
		return;
	// The controller already holds these values; only the processor missed
	// them, because the full edit queue could not be drained at the time.
	LogF(L"Restoring the state of VST3 plugin %s: %u parameter values did not reach the processor"
		L" (the edit queue was full and could not be drained while audio was running).",
		library->getLibPath().c_str(), dropped);
}
