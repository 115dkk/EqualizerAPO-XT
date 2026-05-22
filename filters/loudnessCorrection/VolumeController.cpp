/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Alexander Walch

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
#include "VolumeController.h"
#include <mmdeviceapi.h>

VolumeController::VolumeController()
{
	HRESULT hr;
	CoInitialize(nullptr);
	IMMDeviceEnumerator* deviceEnumerator = nullptr;
	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
	IMMDevice* defaultDevice = nullptr;

	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,  &defaultDevice);
	deviceEnumerator->Release();
	deviceEnumerator = nullptr;

	_endpointVolume = nullptr;
	hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, (LPVOID*)&_endpointVolume);
	defaultDevice->Release();
	defaultDevice = nullptr;
	float inkrement;
	_endpointVolume->GetVolumeRange(&_minVol, &_maxVol, &inkrement);
}

HRESULT VolumeController::getVolume(double& currentVolume)
{
	float vol;
	HRESULT res = _endpointVolume->GetMasterVolumeLevel(&vol);
	currentVolume = vol;
	return res;
}

HRESULT VolumeController::setVolume(double volume)
{
	volume = fmin(volume, _maxVol);
	volume = fmax(volume, _minVol);
	return _endpointVolume->SetMasterVolumeLevel(float(volume), nullptr);
}
