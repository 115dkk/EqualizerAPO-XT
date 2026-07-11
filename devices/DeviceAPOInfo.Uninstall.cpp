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


void DeviceAPOInfo::uninstall()
{
	wstring keyPath;
	if (!input)
		keyPath = renderKeyPath L"\\" + deviceGuid;
	else
		keyPath = captureKeyPath L"\\" + deviceGuid;

	if (originalApoGuids[0] == APOGUID_NOKEY)
	{
		// This installation created FxProperties, but since Windows 11 24H2
		// (build 26100) the OS puts its own subkeys below it, and
		// RegDeleteKeyExW refuses to delete a key that has subkeys - a
		// whole-key delete therefore throws on such systems and the uninstall
		// leaves the EQ CLSIDs dangling (issue #189). Delete the values this
		// installation wrote and remove the key itself only when nothing else
		// lives in it.
		wstring fxPath = keyPath + L"\\FxProperties";
		if (RegistryHelper::keyExists(fxPath))
		{
			static const wchar_t* ownedValueNames[] = {
				lfxGuidValueName, gfxGuidValueName, sfxGuidValueName,
				mfxGuidValueName, efxGuidValueName,
				sfxProcessingModesValueName, mfxProcessingModesValueName,
				efxProcessingModesValueName, fxTitleValueName
			};
			for (const wchar_t* valueName : ownedValueNames)
			{
				if (RegistryHelper::valueExists(fxPath, valueName))
					RegistryHelper::deleteValue(fxPath, valueName);
			}

			if (RegistryHelper::keyEmpty(fxPath))
				RegistryHelper::deleteKey(fxPath);
		}
	}
	else
	{
		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			if (originalApoGuids[i] == APOGUID_NOVALUE)
			{
				if (RegistryHelper::valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
					RegistryHelper::deleteValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);
			}
			else if (originalApoGuids[i] != L"")
			{
				RegistryHelper::writeValue(keyPath + L"\\FxProperties", allGuidValueNames[i], originalApoGuids[i]);
			}
		}
	}

	if (RegistryHelper::keyExists(childApoPath) && RegistryHelper::valueExists(childApoPath, deviceGuid))
		RegistryHelper::deleteValue(childApoPath, deviceGuid);

	if (RegistryHelper::keyExists(childApoPath L"\\" + deviceGuid))
		RegistryHelper::deleteKey(childApoPath L"\\" + deviceGuid);

	if (RegistryHelper::keyExists(childApoPath) && RegistryHelper::keyEmpty(childApoPath))
		RegistryHelper::deleteKey(childApoPath);
}

void DeviceAPOInfo::reinstall()
{
	uninstall();
	load(deviceGuid);
	install();
}

wstring DeviceAPOInfo::getConnectionName() const
{
	return connectionName;
}

wstring DeviceAPOInfo::getDeviceName() const
{
	return deviceName;
}

wstring DeviceAPOInfo::getDeviceGuid() const
{
	return deviceGuid;
}

wstring DeviceAPOInfo::getDeviceString() const
{
	return getConnectionName() + L" " + getDeviceName() + L" " + getDeviceGuid();
}

unsigned DeviceAPOInfo::getChannelCount() const
{
	return channelCount;
}

unsigned DeviceAPOInfo::getSampleRate() const
{
	return sampleRate;
}

unsigned long DeviceAPOInfo::getChannelMask() const
{
	return channelMask;
}

bool DeviceAPOInfo::isInput() const
{
	return input;
}

bool DeviceAPOInfo::isInstalled() const
{
	return installed;
}

bool DeviceAPOInfo::isEnhancementsDisabled() const
{
	return enhancementsDisabled;
}

bool DeviceAPOInfo::isDefaultDevice() const
{
	return defaultDevice;
}

bool DeviceAPOInfo::isDisabled() const
{
	return disabled;
}

bool DeviceAPOInfo::isUnplugged() const
{
	return unplugged;
}

const DeviceAPOInfo::InstallState& DeviceAPOInfo::getCurrentInstallState()
{
	return currentInstallState;
}

DeviceAPOInfo::InstallState& DeviceAPOInfo::getSelectedInstallState()
{
	return selectedInstallState;
}

wstring DeviceAPOInfo::getPreMixChildGuid()
{
	return preMixChildGuid;
}

wstring DeviceAPOInfo::getPostMixChildGuid()
{
	return postMixChildGuid;
}

void DeviceAPOInfo::testAPOInstallation()
{
	IMMDeviceEnumerator* enumerator = nullptr;
	IMMDevice* device = nullptr;
	IAudioClient* audioClient = nullptr;
	WAVEFORMATEX* format = nullptr;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (FAILED(hr))
		fail(L"CoCreateInstance for IMMDeviceEnumerator", hr);
	SCOPE_EXIT{enumerator->Release(); };

	hr = enumerator->GetDevice(((input ? L"{0.0.1.00000000}." : L"{0.0.0.00000000}.") + deviceGuid).c_str(), &device);
	if (FAILED(hr))
		fail(L"GetDevice", hr);
	SCOPE_EXIT{device->Release(); };

	DWORD state;
	hr = device->GetState(&state);
	if (FAILED(hr))
		fail(L"GetState", hr);
	if (state & DEVICE_STATE_DISABLED || state & DEVICE_STATE_UNPLUGGED)
		return;

	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
	if (FAILED(hr))
		fail(L"Activate", hr);
	SCOPE_EXIT{audioClient->Release(); };

	hr = audioClient->GetMixFormat(&format);
	if (FAILED(hr))
		fail(L"GetMixFormat", hr);
	SCOPE_EXIT{CoTaskMemFree(format); };

	hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000 /*100 ms*/, 0, format, nullptr);
	if (FAILED(hr))
	{
		// Field machines (PC-bang demo, issue #75) showed endpoints that
		// reject their own mix format with E_INVALIDARG, especially right
		// after the AudioSrv restart this test performs - typically virtual
		// or vendor-effect devices. The stream is never started: Initialize
		// only runs so the audio engine instantiates the APO chain, so any
		// accepted format is good enough. Retry once with engine-side
		// auto-conversion; a failed Initialize leaves the client unusable,
		// so a fresh one must be activated for the retry.
		IAudioClient* retryClient = nullptr;
		HRESULT retryHr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&retryClient);
		if (SUCCEEDED(retryHr))
		{
			SCOPE_EXIT{retryClient->Release(); };
			retryHr = retryClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
				AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
				1000000 /*100 ms*/, 0, format, nullptr);
		}
		if (FAILED(retryHr))
			fail(L"Initialize", hr); // report the original failure
	}
}

void DeviceAPOInfo::fail(const wstring& functionName, HRESULT hr)
{
	_com_error err(hr);
	const wchar_t* msg = err.ErrorMessage();
	throw DeviceException(functionName + L" failed for device \"" + deviceName + L"\" (" + msg + L")");
}
