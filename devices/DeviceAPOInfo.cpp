/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2012  Jonas Thedering

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
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <shellapi.h>
#include <comdef.h>

#include "DeviceAPOInfo.h"
#include "VoicemeeterAPOInfo.h"
#include "DeviceAPOInfoKeys.h"

#include "helpers/StringHelper.h"
#include "helpers/RegistryHelper.h"
#include "helpers/ComPtr.h"
#include "helpers/Win32Resource.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

static PROPERTYKEY guidPropertyKey = {
	{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 4
};

DeviceAPOInfo::DeviceAPOInfo(IRegistry& registry)
	: registry(registry)
{
}

vector<shared_ptr<AbstractAPOInfo>> DeviceAPOInfo::loadAllInfos(bool input, IRegistry& registry)
{
	vector<shared_ptr<AbstractAPOInfo>> result;

	vector<wstring> deviceGuidStrings = registry.enumSubKeys(input ? captureKeyPath : renderKeyPath);
	wstring defaultDeviceGuid = getDefaultDevice(input);
	for (vector<wstring>::iterator it = deviceGuidStrings.begin(); it != deviceGuidStrings.end(); it++)
	{
		wstring deviceGuidString = *it;

		shared_ptr<DeviceAPOInfo> info = make_shared<DeviceAPOInfo>(registry);
		if (info->load(deviceGuidString, defaultDeviceGuid))
		{
			info->selectedInstallState = info->currentInstallState;
			result.push_back(move(info));
		}
	}

	if (!input)
		VoicemeeterAPOInfo::prependInfos(result, registry);

	return result;
}

wstring DeviceAPOInfo::getDefaultDevice(bool input, int role)
{
	wstring result;

	winutil::ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
	if (SUCCEEDED(hr))
	{
		winutil::ComPtr<IMMDevice> endPoint;
		hr = enumerator->GetDefaultAudioEndpoint(input ? eCapture : eRender, (ERole)role, endPoint.put());
		if (SUCCEEDED(hr))
		{
			winutil::ComPtr<IPropertyStore> propertyStore;
			hr = endPoint->OpenPropertyStore(STGM_READ, propertyStore.put());
			if (SUCCEEDED(hr))
			{
				winutil::PropVariant variant;
				hr = propertyStore->GetValue(guidPropertyKey, &variant);
				if (SUCCEEDED(hr) && variant->vt == VT_LPWSTR && variant->pwszVal != nullptr)
					result = variant->pwszVal;
			}
		}
	}

	return result;
}

bool DeviceAPOInfo::checkProtectedAudioDG(bool fix, IRegistry& registry)
{
	bool result = true;

	if (!registry.valueExists(protectedDGKeyPath, protectedDGValueName) || registry.readDWORDValue(protectedDGKeyPath, protectedDGValueName) != 1)
	{
		result = false;

		if (fix)
			registry.writeDWORDValue(protectedDGKeyPath, protectedDGValueName, 1);
	}

	return result;
}

bool DeviceAPOInfo::checkAPORegistration(bool fix, const IRegistry& registry)
{
	bool result = true;

	if (!registry.keyExists(apoRegistrationKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID))
		|| !registry.keyExists(apoRegistrationKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID))
		|| !registry.keyExists(clsidKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID))
		|| !registry.keyExists(clsidKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID)))
	{
		result = false;

		if (fix)
		{
			wchar_t path[MAX_PATH];
			if (GetModuleFileNameW(nullptr, path, MAX_PATH) != 0)
			{
				PathRemoveFileSpecW(path);
				wstring dllPath = wstring(path) + L"\\EqualizerAPO.dll";

				// Self-register the COM in-proc server by calling its
				// DllRegisterServer export directly instead of spawning
				// regsvr32.exe (no extra process, no transient window).
				// LOAD_WITH_ALTERED_SEARCH_PATH resolves the DLL's own
				// dependencies relative to its directory.
				winutil::UniqueModule module(LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
				if (module)
				{
					using DllServerProc = HRESULT(__stdcall*)();
					DllServerProc registerProc = reinterpret_cast<DllServerProc>(GetProcAddress(module.get(), "DllRegisterServer"));
					if (registerProc != nullptr)
						registerProc();
				}
			}
		}
	}

	return result;
}
