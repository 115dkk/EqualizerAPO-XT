/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	WASAPI endpoint enumeration and activation shared by the audio probes.
*/

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include <cwctype>
#include <string>
#include <vector>

#include "platform/windows/ComPtr.h"

namespace probesupport::wasapi
{
	struct Endpoint
	{
		EDataFlow flow = eRender;
		std::wstring id;
		std::wstring name;
	};

	inline HRESULT createEnumerator(winutil::ComPtr<IMMDeviceEnumerator>& enumerator, DWORD context = CLSCTX_INPROC_SERVER)
	{
		return CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, context, __uuidof(IMMDeviceEnumerator),
			reinterpret_cast<void**>(enumerator.put()));
	}

	inline HRESULT defaultEndpoint(IMMDeviceEnumerator* enumerator, EDataFlow flow,
		winutil::ComPtr<IMMDevice>& device, ERole role = eConsole)
	{
		return enumerator->GetDefaultAudioEndpoint(flow, role, device.put());
	}

	inline HRESULT endpointById(IMMDeviceEnumerator* enumerator, const std::wstring& id,
		winutil::ComPtr<IMMDevice>& device)
	{
		return enumerator->GetDevice(id.c_str(), device.put());
	}

	inline HRESULT activateAudioClient(IMMDevice* device, winutil::ComPtr<IAudioClient>& client,
		DWORD context = CLSCTX_INPROC_SERVER)
	{
		return device->Activate(__uuidof(IAudioClient), context, nullptr, reinterpret_cast<void**>(client.put()));
	}

	inline std::wstring friendlyName(IMMDevice* device)
	{
		winutil::ComPtr<IPropertyStore> store;
		if (FAILED(device->OpenPropertyStore(STGM_READ, store.put())))
			return L"?";
		winutil::PropVariant value;
		if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value))
			&& value->vt == VT_LPWSTR && value->pwszVal != nullptr)
			return value->pwszVal;
		return L"?";
	}

	inline std::wstring deviceId(IMMDevice* device)
	{
		winutil::CoTaskMem<wchar_t> id;
		if (FAILED(device->GetId(id.put())) || !id)
			return {};
		return id.get();
	}

	inline std::wstring lower(std::wstring text)
	{
		for (wchar_t& character : text)
			character = static_cast<wchar_t>(std::towlower(character));
		return text;
	}

	inline winutil::ComPtr<IMMDevice> findEndpoint(IMMDeviceEnumerator* enumerator, EDataFlow flow,
		const std::wstring& name, const std::wstring& id)
	{
		winutil::ComPtr<IMMDevice> device;
		if (!id.empty())
		{
			endpointById(enumerator, id, device);
			return device;
		}
		if (name.empty())
		{
			defaultEndpoint(enumerator, flow, device);
			return device;
		}
		winutil::ComPtr<IMMDeviceCollection> collection;
		if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.put())))
			return device;
		UINT count = 0;
		collection->GetCount(&count);
		const std::wstring needle = lower(name);
		for (UINT i = 0; i < count; i++)
		{
			winutil::ComPtr<IMMDevice> candidate;
			if (SUCCEEDED(collection->Item(i, candidate.put()))
				&& lower(friendlyName(candidate.get())).find(needle) != std::wstring::npos)
				return candidate;
		}
		return device;
	}

	inline std::vector<Endpoint> activeEndpoints(IMMDeviceEnumerator* enumerator)
	{
		std::vector<Endpoint> result;
		for (int flow = 0; flow < 2; flow++)
		{
			const EDataFlow dataFlow = flow == 0 ? eRender : eCapture;
			winutil::ComPtr<IMMDeviceCollection> collection;
			if (FAILED(enumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, collection.put())))
				continue;
			UINT count = 0;
			collection->GetCount(&count);
			for (UINT i = 0; i < count; i++)
			{
				winutil::ComPtr<IMMDevice> device;
				if (SUCCEEDED(collection->Item(i, device.put())))
					result.push_back({dataFlow, deviceId(device.get()), friendlyName(device.get())});
			}
		}
		return result;
	}
}
