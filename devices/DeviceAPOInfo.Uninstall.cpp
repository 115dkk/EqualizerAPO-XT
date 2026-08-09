#include "stdafx.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <shellapi.h>
#include <comdef.h>

#include "DeviceAPOInfo.h"
#include "VoicemeeterAPOInfo.h"
#include "DeviceAPOInfoKeys.h"

#include "platform/windows/ComPtr.h"
#include "services/registry/WindowsRegistry.h"

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::vector;
using std::wstring;

void DeviceAPOInfo::uninstall()
{
	runReported(DeviceInstallReport::Operation::Uninstall, [this](RegistryTransaction& plan) {
		uninstallWithin(plan);
	});
}

void DeviceAPOInfo::uninstallWithin(RegistryTransaction& plan)
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
		if (plan.keyExists(fxPath))
		{
			for (const wchar_t* valueName : ownedFxValueNames)
			{
				if (plan.valueExists(fxPath, valueName))
					plan.deleteValue(fxPath, valueName);
			}

			if (plan.keyEmpty(fxPath))
				plan.deleteKey(fxPath);
		}
	}
	else
	{
		for (int i = 0; i < allGuidValueNameCount; i++)
		{
			if (originalApoGuids[i] == APOGUID_NOVALUE)
			{
				if (plan.valueExists(keyPath + L"\\FxProperties", allGuidValueNames[i]))
					plan.deleteValue(keyPath + L"\\FxProperties", allGuidValueNames[i]);
			}
			else if (originalApoGuids[i] != L"")
			{
				plan.writeValue(keyPath + L"\\FxProperties", allGuidValueNames[i], originalApoGuids[i]);
			}
		}
	}

	if (plan.keyExists(childApoPath) && plan.valueExists(childApoPath, deviceGuid))
		plan.deleteValue(childApoPath, deviceGuid);

	if (plan.keyExists(childApoPath L"\\" + deviceGuid))
		plan.deleteKey(childApoPath L"\\" + deviceGuid);

	if (plan.keyExists(childApoPath) && plan.keyEmpty(childApoPath))
		plan.deleteKey(childApoPath);
}

void DeviceAPOInfo::reinstall()
{
	// One transaction over all three steps. The reload in the middle is what
	// makes this more than uninstall-then-install: install() needs the original
	// APO GUIDs as the driver has them, and uninstall() has just put them back.
	// load() only reads, but it throws on an installation this build cannot
	// describe, and before this it threw with the device already uninstalled.
	runReported(DeviceInstallReport::Operation::Reinstall, [this](RegistryTransaction& plan) {
		uninstallWithin(plan);
		load(deviceGuid);
		installWithin(plan);
	});
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

const wstring& DeviceAPOInfo::getPreMixChildGuid() const
{
	return preMixChildGuid;
}

const wstring& DeviceAPOInfo::getPostMixChildGuid() const
{
	return postMixChildGuid;
}

void DeviceAPOInfo::testAPOInstallation()
{
	winutil::ComPtr<IMMDeviceEnumerator> enumerator;
	winutil::ComPtr<IMMDevice> device;
	winutil::ComPtr<IAudioClient> audioClient;
	winutil::CoTaskMem<WAVEFORMATEX> format;

	HRESULT hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator),
		nullptr,
		CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		reinterpret_cast<void**>(enumerator.put()));
	if (FAILED(hr))
		fail(L"CoCreateInstance for IMMDeviceEnumerator", hr);

	hr = enumerator->GetDevice(
		((input ? L"{0.0.1.00000000}." : L"{0.0.0.00000000}.") + deviceGuid).c_str(),
		device.put());
	if (FAILED(hr))
		fail(L"GetDevice", hr);

	DWORD state;
	hr = device->GetState(&state);
	if (FAILED(hr))
		fail(L"GetState", hr);
	if (state & DEVICE_STATE_DISABLED || state & DEVICE_STATE_UNPLUGGED)
		return;

	hr = device->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL,
		nullptr,
		reinterpret_cast<void**>(audioClient.put()));
	if (FAILED(hr))
		fail(L"Activate", hr);

	hr = audioClient->GetMixFormat(format.put());
	if (FAILED(hr))
		fail(L"GetMixFormat", hr);

	hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000 /*100 ms*/, 0, format.get(), nullptr);
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
		winutil::ComPtr<IAudioClient> retryClient;
		HRESULT retryHr = device->Activate(
			__uuidof(IAudioClient),
			CLSCTX_ALL,
			nullptr,
			reinterpret_cast<void**>(retryClient.put()));
		if (SUCCEEDED(retryHr))
		{
			retryHr = retryClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
				AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
				1000000 /*100 ms*/, 0, format.get(), nullptr);
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
