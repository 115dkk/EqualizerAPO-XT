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
#include <limits>
#include <wincrypt.h>
#include "helpers/StringHelper.h"
#include "VSTPluginLibrary.h"
#include "VSTPluginInstance.h"
#include "VSTPluginInstanceInternal.h"
#include "pluginterfaces/base/smartpointer.h"

using namespace std;
using namespace Steinberg;
using namespace Steinberg::Vst;

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

	bool decodeBase64(const wstring& encoded, vector<char>& decoded)
	{
		DWORD requiredSize = 0;
		if (CryptStringToBinaryW(
			encoded.c_str(),
			0,
			CRYPT_STRING_BASE64,
			NULL,
			&requiredSize,
			NULL,
			NULL) != TRUE)
		{
			return false;
		}

		vector<char> result(requiredSize);
		DWORD actualSize = requiredSize;
		if (requiredSize != 0 && CryptStringToBinaryW(
			encoded.c_str(),
			0,
			CRYPT_STRING_BASE64,
			reinterpret_cast<BYTE*>(result.data()),
			&actualSize,
			NULL,
			NULL) != TRUE)
		{
			return false;
		}

		result.resize(actualSize);
		decoded = move(result);
		return true;
	}

	bool encodeBase64(const void* data, size_t size, wstring& encoded)
	{
		if (size > (numeric_limits<DWORD>::max)())
			return false;

		const DWORD binarySize = static_cast<DWORD>(size);
		DWORD requiredLength = 0;
		if (CryptBinaryToStringW(
			static_cast<const BYTE*>(data),
			binarySize,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			NULL,
			&requiredLength) != TRUE)
		{
			return false;
		}

		vector<wchar_t> result(requiredLength);
		DWORD actualLength = requiredLength;
		if (requiredLength == 0 || CryptBinaryToStringW(
			static_cast<const BYTE*>(data),
			binarySize,
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			result.data(),
			&actualLength) != TRUE)
		{
			return false;
		}

		encoded.assign(result.data());
		return true;
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

void VSTPluginInstance::writeToEffect(const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap)
{
	if (library->isVST3())
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
				for (const VST3HostParameterState& parameter : parameters)
				{
					if (vst3Controller != NULL)
						vst3Controller->setParamNormalized(parameter.id, parameter.value);
					queueVST3ParameterEdit(parameter.id, parameter.value);
					parameterQueued = true;
				}
				if (parameterQueued)
					flushVST3ParameterChanges();
			}
		}
		else if (vst3Controller != NULL)
		{
			bool parameterQueued = false;
			for (const auto& it : paramMap)
			{
				for (int32 i = 0; i < vst3Controller->getParameterCount(); i++)
				{
					ParameterInfo info;
					if (vst3Controller->getParameterInfo(i, info) == kResultOk
						&& it.first == fromString128(info.title))
					{
						vst3Controller->setParamNormalized(info.id, it.second);
						queueVST3ParameterEdit(info.id, it.second);
						parameterQueued = true;
						break;
					}
				}
			}
			if (parameterQueued)
				flushVST3ParameterChanges();
		}
		return;
	}

	if (effect == NULL)
		return;

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		if (chunkData != L"")
		{
			vector<char> data;
			if (decodeBase64(chunkData, data))
				effect->control(effect.get(), VST_EFFECT_OPCODE_SET_CHUNK_DATA, 1, data.size(), data.data(), 0.0f);
		}
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect.get(), VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			wstring name = StringHelper::toWString(buf, CP_UTF8);
			auto it = paramMap.find(name);
			if (it != paramMap.end())
				effect->set_parameter(effect.get(), i, it->second);
		}
	}
}

void VSTPluginInstance::readFromEffect(std::wstring& chunkData, std::unordered_map<std::wstring, float>& paramMap) const
{
	if (library->isVST3())
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
		return;
	}

	if (effect == NULL)
		return;

	chunkData = L"";
	paramMap.clear();

	if (effect->flags & VST_EFFECT_FLAG_CHUNKS)
	{
		BYTE* chunk = NULL;
		int size = (int)effect->control(effect.get(), VST_EFFECT_OPCODE_GET_CHUNK_DATA, 1, 0, &chunk, 0.0f);
		if (chunk != NULL && size > 0)
			encodeBase64(chunk, static_cast<size_t>(size), chunkData);
	}
	else
	{
		for (int i = 0; i < effect->num_params; i++)
		{
			char buf[256];
			effect->control(effect.get(), VST_EFFECT_OPCODE_PARAM_GET_NAME, i, 0, buf, 0.0f);
			buf[255] = '\0'; // just to be sure
			float value = effect->get_parameter(effect.get(), i);
			paramMap[StringHelper::toWString(buf, CP_UTF8)] = value;
		}
	}
}
