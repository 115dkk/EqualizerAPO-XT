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

#include "helpers/StringHelper.h"
#include "helpers/RegistryHelper.h"
#include "helpers/ComPtr.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

#define protectedDGKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio"
#define protectedDGValueName L"DisableProtectedAudioDG"
#define apoRegistrationKeyPath L"HKEY_CLASSES_ROOT\\AudioEngine\\AudioProcessingObjects"
#define clsidKeyPath L"HKEY_CLASSES_ROOT\\CLSID"
#define commonKeyPath L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio"
#define renderKeyPath commonKeyPath L"\\Render"
#define captureKeyPath commonKeyPath L"\\Capture"
#define childApoPath APP_REGPATH L"\\Child APOs"
static const wchar_t* preMixChildGuidValueName = L"PreMixChild";
static const wchar_t* postMixChildGuidValueName = L"PostMixChild";
static const wchar_t* allowSilentBufferValueName = L"AllowSilentBufferModification";
static const wchar_t* disableAutoAdjustValueName = L"DisableAutomaticAdjustment";
static const wchar_t* versionValueName = L"Version";
static const wchar_t* connectionValueName = L"{a45c254e-df1c-4efd-8020-67d146a850e0},2";
static const wchar_t* deviceValueName = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6";
static const wchar_t* combinedDeviceValueName = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},41";
static const wchar_t* formatValueName = L"{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0";
static const wchar_t* channelMaskValueName = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},3";
static const wchar_t* lfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1";
static const wchar_t* gfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2";
static const wchar_t* sfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5";
static const wchar_t* mfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6";
static const wchar_t* efxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";
static const wchar_t* multiSfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},13";
static const wchar_t* multiMfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},14";
static const wchar_t* multiEfxGuidValueName = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15";
static const unsigned allGuidValueNameCount = 5;
static const wchar_t* allGuidValueNames[] = {lfxGuidValueName, gfxGuidValueName, sfxGuidValueName, mfxGuidValueName, efxGuidValueName};
enum GuidValueIndices
{
	LFX_INDEX = 0,
	GFX_INDEX = 1,
	SFX_INDEX = 2,
	MFX_INDEX = 3,
	EFX_INDEX = 4
};
static const wchar_t* fxTitleValueName = L"{b725f130-47ef-101a-a5f1-02608c9eebac},10";
static const wchar_t* sfxProcessingModesValueName = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},5";
static const wchar_t* mfxProcessingModesValueName = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},6";
static const wchar_t* efxProcessingModesValueName = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},7";
static const wchar_t* defaultProcessingModeValue = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
static const wchar_t* disableEnhancementsValueName = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";
static const wchar_t* installVersion = L"2";
static PROPERTYKEY guidPropertyKey = {{0x1da5d803, 0xd492, 0x4edd, 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}, 4};

vector<shared_ptr<AbstractAPOInfo>> DeviceAPOInfo::loadAllInfos(bool input)
{
	vector<shared_ptr<AbstractAPOInfo>> result;

	vector<wstring> deviceGuidStrings = RegistryHelper::enumSubKeys(input ? captureKeyPath : renderKeyPath);
	wstring defaultDeviceGuid = getDefaultDevice(input);
	for (vector<wstring>::iterator it = deviceGuidStrings.begin(); it != deviceGuidStrings.end(); it++)
	{
		wstring deviceGuidString = *it;

		shared_ptr<DeviceAPOInfo> info = make_shared<DeviceAPOInfo>();
		if (info->load(deviceGuidString, defaultDeviceGuid))
		{
			info->selectedInstallState = info->currentInstallState;
			result.push_back(move(info));
		}
	}

	if (!input)
		VoicemeeterAPOInfo::prependInfos(result);

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

bool DeviceAPOInfo::checkProtectedAudioDG(bool fix)
{
	bool result = true;

	if (!RegistryHelper::valueExists(protectedDGKeyPath, protectedDGValueName) || RegistryHelper::readDWORDValue(protectedDGKeyPath, protectedDGValueName) != 1)
	{
		result = false;

		if (fix)
			RegistryHelper::writeDWORDValue(protectedDGKeyPath, protectedDGValueName, 1);
	}

	return result;
}

bool DeviceAPOInfo::checkAPORegistration(bool fix)
{
	bool result = true;

	if (!RegistryHelper::keyExists(apoRegistrationKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID))
		|| !RegistryHelper::keyExists(apoRegistrationKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID))
		|| !RegistryHelper::keyExists(clsidKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_PRE_MIX_GUID))
		|| !RegistryHelper::keyExists(clsidKeyPath L"\\" + RegistryHelper::getGuidString(EQUALIZERAPO_POST_MIX_GUID)))
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
				HMODULE module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
				if (module != nullptr)
				{
					using DllServerProc = HRESULT(__stdcall*)();
					DllServerProc registerProc = reinterpret_cast<DllServerProc>(GetProcAddress(module, "DllRegisterServer"));
					if (registerProc != nullptr)
						registerProc();
					FreeLibrary(module);
				}
			}
		}
	}

	return result;
}
