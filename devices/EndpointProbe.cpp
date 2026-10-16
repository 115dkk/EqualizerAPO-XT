/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2012 Jonas Thedering
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "stdafx.h"
#include "devices/EndpointProbe.h"
#include "devices/DeviceException.h"
#include "platform/windows/ComPtr.h"
#include "platform/windows/Win32Resource.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <comdef.h>

using std::wstring;

namespace
{
class ComEndpointProbe final : public EndpointProbe
{
public:
	wstring defaultEndpointGuid(bool input, int role) const override;
	void probeEndpoint(const wstring& deviceGuid, bool input, const wstring& deviceName) const override;
};
static PROPERTYKEY guidPropertyKey = {
	{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 4
};

void fail(const wstring& deviceName, const wstring& functionName, HRESULT hr)
{
	_com_error err(hr);
	const wchar_t* msg = err.ErrorMessage();
	throw DeviceException(functionName + L" failed for device \"" + deviceName + L"\" (" + msg + L")");
}
wstring ComEndpointProbe::defaultEndpointGuid(bool input, int role) const
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

void ComEndpointProbe::probeEndpoint(const wstring& deviceGuid, bool input, const wstring& deviceName) const
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
		fail(deviceName, L"CoCreateInstance for IMMDeviceEnumerator", hr);

	hr = enumerator->GetDevice(
		((input ? L"{0.0.1.00000000}." : L"{0.0.0.00000000}.") + deviceGuid).c_str(),
		device.put());
	if (FAILED(hr))
		fail(deviceName, L"GetDevice", hr);

	DWORD state;
	hr = device->GetState(&state);
	if (FAILED(hr))
		fail(deviceName, L"GetState", hr);
	if (state & DEVICE_STATE_DISABLED || state & DEVICE_STATE_UNPLUGGED)
		return;

	hr = device->Activate(
		__uuidof(IAudioClient),
		CLSCTX_ALL,
		nullptr,
		reinterpret_cast<void**>(audioClient.put()));
	if (FAILED(hr))
		fail(deviceName, L"Activate", hr);

	hr = audioClient->GetMixFormat(format.put());
	if (FAILED(hr))
		fail(deviceName, L"GetMixFormat", hr);

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
			fail(deviceName, L"Initialize", hr); // report the original failure
	}
}

}

const EndpointProbe& systemEndpointProbe()
{
	static const ComEndpointProbe probe;
	return probe;
}
