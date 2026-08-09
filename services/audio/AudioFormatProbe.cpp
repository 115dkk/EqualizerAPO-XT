/*
	This file is part of EqualizerAPO, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
*/

#include "services/audio/AudioFormatProbe.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Unknwn.h>
#define INITGUID
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>

#include "platform/windows/ComPtr.h"

using winutil::ComPtr;
using winutil::CoTaskMem;

namespace
{
	std::wstring formatSubtypeName(const GUID& sub)
	{
		if (IsEqualGUID(sub, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
			return L"IEEE_FLOAT";
		if (IsEqualGUID(sub, KSDATAFORMAT_SUBTYPE_PCM))
			return L"PCM";
		return L"Other";
	}
}

namespace AudioFormatProbe
{

Result probe(const std::wstring& deviceGuid)
{
	Result result;

	// IMMDeviceEnumerator/IAudioClient are usable from a regular STA/MTA
	// process. CoInitializeEx is assumed to have been called by the host
	// (Qt does this implicitly via the GUI event loop).
	ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
		CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
		reinterpret_cast<void**>(enumerator.put()));
	if (FAILED(hr) || !enumerator)
		return result;

	ComPtr<IMMDevice> device;
	hr = enumerator->GetDevice(deviceGuid.c_str(), device.put());
	if (FAILED(hr) || !device)
		return result;

	ComPtr<IAudioClient> client;
	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
		reinterpret_cast<void**>(client.put()));
	if (FAILED(hr) || !client)
		return result;

	CoTaskMem<WAVEFORMATEX> mixFormat;
	hr = client->GetMixFormat(mixFormat.put());
	if (FAILED(hr) || !mixFormat)
		return result;

	result.containerBytes = mixFormat->wBitsPerSample / 8;
	result.sampleRate = mixFormat->nSamplesPerSec;
	result.channelCount = mixFormat->nChannels;
	result.validBits = mixFormat->wBitsPerSample;

	GUID subFormat = {};
	if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE
		&& mixFormat->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
	{
		WAVEFORMATEXTENSIBLE* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mixFormat.get());
		subFormat = ext->SubFormat;
		if (ext->Samples.wValidBitsPerSample != 0)
			result.validBits = ext->Samples.wValidBitsPerSample;
	}
	else if (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
	{
		subFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	}
	else if (mixFormat->wFormatTag == WAVE_FORMAT_PCM)
	{
		subFormat = KSDATAFORMAT_SUBTYPE_PCM;
	}

	result.isFloat = IsEqualGUID(subFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
	result.subtypeDescription = formatSubtypeName(subFormat);

	if (result.isFloat)
	{
		if (result.containerBytes == 4)
			result.status = Status::ActiveFloat32;
		else if (result.containerBytes == 8)
			result.status = Status::ActiveFloat64;
		else
			result.status = Status::Passthrough;
	}
	else
	{
		result.status = Status::Passthrough;
	}

	return result;
}

std::wstring describe(const Result& r)
{
	switch (r.status)
	{
	case Status::ActiveFloat32:
		return L"EQ active (IEEE_FLOAT 32-bit)";
	case Status::ActiveFloat64:
		return L"EQ active (IEEE_FLOAT 64-bit)";
	case Status::Passthrough:
	{
		std::wstring desc = L"Passthrough — EQ inactive (";
		desc += r.subtypeDescription;
		desc += L", container " + std::to_wstring(r.containerBytes * 8) + L"-bit)";
		return desc;
	}
	case Status::Unknown:
	default:
		return L"";
	}
}

}  // namespace AudioFormatProbe
