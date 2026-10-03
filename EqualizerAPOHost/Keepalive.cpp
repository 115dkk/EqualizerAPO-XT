/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EqualizerAPOHost/Keepalive.h"
#include "EqualizerAPOHost/KeepaliveFill.h"

#include <algorithm>
#include <exception>
#include <map>
#include <stop_token>
#include <string>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include "devices/DeviceAPOInfoKeys.h"
#include "devices/ReceiverEndpoints.h"
#include "platform/windows/ComPtr.h"
#include "platform/windows/Win32Resource.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"

namespace
{
	bool ditherWanted()
	{
		const IRegistry& registry = systemRegistry();
		try
		{
			return registry.keyExists(APP_REGPATH) && registry.valueExists(APP_REGPATH, L"SendKeepaliveFill")
				&& registry.readValue(APP_REGPATH, L"SendKeepaliveFill") == L"dither";
		}
		catch (const RegistryError& error)
		{
			LogFStatic(L"Send keepalive: cannot read fill setting, using zeros: %s", error.getMessage().c_str());
			return false;
		}
	}

	struct RenderStream
	{
		// The event outlives the client that holds it, and the render service
		// is released on this same thread before its parent client.
		winutil::UniqueHandle event;
		winutil::ComPtr<IMMDeviceEnumerator> enumerator;
		winutil::ComPtr<IMMDevice> device;
		winutil::ComPtr<IAudioClient> client;
		winutil::ComPtr<IAudioRenderClient> render;
		winutil::CoTaskMem<WAVEFORMATEX> format;
		UINT32 bufferFrames = 0;
		REFERENCE_TIME defaultPeriod = 0;
		bool started = false;
		bool dither = false;
		uint32_t rng = GetTickCount() ^ GetCurrentThreadId();

		~RenderStream()
		{
			if (started)
				client->Stop();
		}

		HRESULT fill(UINT32 frames)
		{
			if (frames == 0)
				return S_OK;
			BYTE* data = nullptr;
			HRESULT hr = render->GetBuffer(frames, &data);
			if (FAILED(hr))
				return hr;
			fillKeepalive(data, frames, *format.get(), dither, rng);
			// Never mark the packet silent: the receiving APO must be invoked.
			return render->ReleaseBuffer(frames, 0);
		}

		HRESULT open(const std::wstring& guid)
		{
			event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
			if (!event)
				return HRESULT_FROM_WIN32(GetLastError());
			HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
				__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
			if (FAILED(hr))
				return hr;
			// Same render endpoint ID spelling as wasapi::endpointId.
			const std::wstring id = L"{0.0.0.00000000}." + guid;
			hr = enumerator->GetDevice(id.c_str(), device.put());
			if (FAILED(hr))
				return hr;
			hr = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr,
				reinterpret_cast<void**>(client.put()));
			if (FAILED(hr))
				return hr;
			hr = client->GetMixFormat(format.put());
			if (FAILED(hr))
				return hr;
			if (!format || !keepaliveFormatSupported(*format.get()))
				return AUDCLNT_E_UNSUPPORTED_FORMAT;
			hr = client->GetDevicePeriod(&defaultPeriod, nullptr);
			if (FAILED(hr))
				return hr;
			// Whether all-zero, unflagged packets keep a post-mix APO running
			// every period has not been measured. SendKeepaliveFill="dither"
			// is the fallback without a patch; read it again on each reopen.
			dither = ditherWanted();
			hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
				0, 0, format.get(), nullptr);
			if (FAILED(hr))
				return hr;
			hr = client->SetEventHandle(event.get());
			if (FAILED(hr))
				return hr;
			hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.put()));
			if (FAILED(hr))
				return hr;
			hr = client->GetBufferSize(&bufferFrames);
			if (FAILED(hr))
				return hr;
			if (bufferFrames == 0)
				return E_UNEXPECTED;
			hr = fill(bufferFrames);
			if (FAILED(hr))
				return hr;
			hr = client->Start();
			if (SUCCEEDED(hr))
				started = true;
			return hr;
		}

		HRESULT run(HANDLE stopEvent)
		{
			const HANDLE events[] = {stopEvent, event.get()};
			for (;;)
			{
				const DWORD waited = WaitForMultipleObjects(2, events, FALSE, 2000);
				if (waited == WAIT_OBJECT_0)
				{
					const HRESULT hr = client->Stop();
					started = false;
					return hr;
				}
				if (waited == WAIT_FAILED)
					return HRESULT_FROM_WIN32(GetLastError());
				if (waited == WAIT_TIMEOUT)
					return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
				if (waited != WAIT_OBJECT_0 + 1)
					return E_UNEXPECTED;
				UINT32 padding = 0;
				HRESULT hr = client->GetCurrentPadding(&padding);
				if (FAILED(hr))
					return hr;
				if (padding > bufferFrames)
					return E_UNEXPECTED;
				hr = fill(bufferFrames - padding);
				if (FAILED(hr))
					return hr;
			}
		}
	};

	void receive(std::stop_token stop, const std::wstring& guid)
	{
		try
		{
			winutil::UniqueHandle stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!stopEvent)
			{
				LogFStatic(L"Send keepalive %s: gave up creating stop event (error %lu)", guid.c_str(), GetLastError());
				return;
			}
			std::stop_callback onStop(stop, [&] { SetEvent(stopEvent.get()); });
			HRESULT lastFailure = S_OK;
			bool lost = false;
			while (!stop.stop_requested())
			{
				HRESULT hr;
				{
					winutil::ComApartment apartment(COINIT_MULTITHREADED);
					hr = apartment.status();
					if (SUCCEEDED(hr))
					{
						RenderStream stream;
						hr = stream.open(guid);
						if (SUCCEEDED(hr))
						{
							lost = false;
							LogFStatic(L"Send keepalive %s: opened %lu Hz, %u channels, %u bits, tag 0x%04x, default period %lld hns, buffer %u frames, %s",
								guid.c_str(), stream.format->nSamplesPerSec, stream.format->nChannels,
								stream.format->wBitsPerSample, stream.format->wFormatTag, stream.defaultPeriod,
								stream.bufferFrames, stream.dither ? L"dither" : L"zeros");
							hr = stream.run(stopEvent.get());
						}
					}
				}
				// All stream resources and COM are gone before the retry wait.
				// Device invalidation (including format changes), service loss,
				// timeouts and all other failed calls use this same reopen path.
				if (FAILED(hr) && (!lost || hr != lastFailure))
				{
					LogFStatic(L"Send keepalive %s: lost (0x%08lx), retrying", guid.c_str(), hr);
					lastFailure = hr;
					lost = true;
				}
				if (stop.stop_requested())
					break;
				const DWORD waited = WaitForSingleObject(stopEvent.get(), 1000);
				if (waited == WAIT_OBJECT_0)
					break;
				if (waited != WAIT_TIMEOUT)
				{
					LogFStatic(L"Send keepalive %s: gave up waiting to retry (error %lu)", guid.c_str(), GetLastError());
					return;
				}
			}
			LogFStatic(L"Send keepalive %s: stopped", guid.c_str());
		}
		catch (const std::exception&)
		{
			LogFStatic(L"Send keepalive %s: gave up after a worker exception", guid.c_str());
		}
	}

	struct Workers
	{
		std::map<std::wstring, std::jthread> threads;

		~Workers()
		{
			for (auto& entry : threads)
				entry.second.request_stop();
			threads.clear();
		}
	};

	void supervise(std::stop_token stop)
	{
		try
		{
			winutil::ComApartment apartment(COINIT_MULTITHREADED);
			if (FAILED(apartment.status()))
			{
				LogFStatic(L"Send keepalive: supervisor gave up initializing COM (0x%08lx)", apartment.status());
				return;
			}
			winutil::UniqueHandle stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!stopEvent)
			{
				LogFStatic(L"Send keepalive: supervisor gave up creating stop event (error %lu)", GetLastError());
				return;
			}
			std::stop_callback onStop(stop, [&] { SetEvent(stopEvent.get()); });
			Workers workers;
			while (!stop.stop_requested())
			{
				// A fresh key/event pair each pass cancels an outstanding watch
				// on timeout, avoiding duplicate RegNotify waits. Arm BEFORE the
				// snapshot so a change during enumeration cannot be missed. Close
				// the key before its event, including when the tree is deleted.
				winutil::UniqueHandle changed(CreateEventW(nullptr, FALSE, FALSE, nullptr));
				winutil::UniqueRegistryKey key;
				bool armed = false;
				if (changed)
				{
					try
					{
						key = WindowsRegistry::openKey(childApoPath, KEY_NOTIFY | KEY_WOW64_64KEY);
						armed = RegNotifyChangeKeyValue(key.get(), TRUE,
							REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, changed.get(), TRUE) == ERROR_SUCCESS;
					}
					catch (const RegistryError&)
					{
						// Missing or inaccessible tree: the periodic scan still runs.
					}
				}

				const std::vector<std::wstring> wanted = receivingEndpoints(systemRegistry());
				for (auto it = workers.threads.begin(); it != workers.threads.end();)
				{
					if (std::find(wanted.begin(), wanted.end(), it->first) == wanted.end())
						it = workers.threads.erase(it);
					else
						++it;
				}
				for (const std::wstring& guid : wanted)
				{
					if (stop.stop_requested())
						break;
					if (!workers.threads.contains(guid))
						workers.threads.emplace(guid, std::jthread([guid](std::stop_token workerStop) { receive(workerStop, guid); }));
				}
				const HANDLE events[] = {stopEvent.get(), changed.get()};
				const DWORD waited = WaitForMultipleObjects(armed ? 2 : 1, events, FALSE, 5000);
				if (waited == WAIT_OBJECT_0)
					break;
				if (waited == WAIT_FAILED)
				{
					LogFStatic(L"Send keepalive: supervisor gave up waiting (error %lu)", GetLastError());
					break;
				}
			}
		}
		catch (const std::exception&)
		{
			LogFStatic(L"Send keepalive: supervisor gave up after an exception");
		}
	}
}

std::jthread startKeepalive()
{
	try
	{
		return std::jthread(supervise);
	}
	catch (const std::exception&)
	{
		LogFStatic(L"Send keepalive: gave up starting supervisor");
		return {};
	}
}
