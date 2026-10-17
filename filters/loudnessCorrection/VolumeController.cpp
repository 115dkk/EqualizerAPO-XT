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

#include <atomic>
#include <new>
#include <system_error>

#include <mmdeviceapi.h>

#include "platform/windows/Win32Event.h"

class VolumeController::NotificationCallback final : public IAudioEndpointVolumeCallback
{
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
	{
		if (object == nullptr)
			return E_POINTER;
		*object = nullptr;
		if (interfaceId == __uuidof(IUnknown)
			|| interfaceId == __uuidof(IAudioEndpointVolumeCallback))
		{
			*object = static_cast<IAudioEndpointVolumeCallback*>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return referenceCount.fetch_add(1, std::memory_order_relaxed) + 1;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG remaining = referenceCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
		if (remaining == 0)
			delete this;
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA notification) override
	{
		if (notification == nullptr)
			return E_INVALIDARG;
		changed.set();
		return S_OK;
	}

	HANDLE handle() const noexcept {return changed.get();}

private:
	std::atomic<ULONG> referenceCount{1};
	Win32Event changed{false, false};
};

VolumeController::VolumeController()
{
	if (!_comApartment.isUsable())
		return;

	winutil::ComPtr<IMMDeviceEnumerator> deviceEnumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
		__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(deviceEnumerator.put()));
	if (FAILED(hr) || !deviceEnumerator)
		return;

	winutil::ComPtr<IMMDevice> defaultDevice;
	hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.put());
	if (FAILED(hr) || !defaultDevice)
		return;

	hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
		nullptr, reinterpret_cast<void**>(_endpointVolume.put()));
	if (FAILED(hr) || !_endpointVolume)
		return;

	float increment;
	_endpointVolume->GetVolumeRange(&_minVol, &_maxVol, &increment);
}

VolumeController::~VolumeController()
{
	if (_notificationsRegistered && _endpointVolume && _notificationCallback != nullptr)
		_endpointVolume->UnregisterControlChangeNotify(_notificationCallback);
	if (_notificationCallback != nullptr)
		_notificationCallback->Release();
}

HRESULT VolumeController::startNotifications()
{
	if (!_endpointVolume)
		return E_POINTER;
	if (_notificationsRegistered)
		return S_OK;
	if (_notificationCallback == nullptr)
	{
		try
		{
			_notificationCallback = new NotificationCallback();
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (const std::system_error& error)
		{
			return HRESULT_FROM_WIN32(static_cast<unsigned long>(error.code().value()));
		}
		catch (...)
		{
			return E_FAIL;
		}
	}
	const HRESULT result = _endpointVolume->RegisterControlChangeNotify(_notificationCallback);
	if (SUCCEEDED(result))
		_notificationsRegistered = true;
	return result;
}

HANDLE VolumeController::notificationHandle() const
{
	return _notificationCallback == nullptr ? nullptr : _notificationCallback->handle();
}

HRESULT VolumeController::getVolume(double& currentVolume)
{
	if (!_endpointVolume)
		return E_POINTER;

	float vol;
	HRESULT res = _endpointVolume->GetMasterVolumeLevel(&vol);
	currentVolume = vol;
	return res;
}

HRESULT VolumeController::setVolume(double volume)
{
	if (!_endpointVolume)
		return E_POINTER;

	volume = fmin(volume, _maxVol);
	volume = fmax(volume, _minVol);
	return _endpointVolume->SetMasterVolumeLevel(float(volume), nullptr);
}
