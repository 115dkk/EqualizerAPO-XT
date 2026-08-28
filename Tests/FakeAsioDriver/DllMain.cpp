/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	COM entry points of the fake ASIO driver DLL. The probe loads it with
	LoadLibrary + DllGetClassObject, so nothing needs registering on a CI
	runner; a DAW could register it like any driver for manual experiments.
*/

#include <new>

#include "Tests/FakeAsioDriver/FakeAsio.h"

namespace
{
	long factoryLocks = 0;

	class FakeClassFactory final : public IClassFactory
	{
	public:
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
		{
			if (object == nullptr)
				return E_POINTER;
			if (riid == IID_IUnknown || riid == IID_IClassFactory)
			{
				*object = static_cast<IClassFactory*>(this);
				AddRef();
				return S_OK;
			}
			*object = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return static_cast<ULONG>(InterlockedIncrement(&refCount_));
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const LONG remaining = InterlockedDecrement(&refCount_);
			if (remaining == 0)
			{
				delete this;
				return 0;
			}
			return static_cast<ULONG>(remaining);
		}

		HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** object) override
		{
			if (object == nullptr)
				return E_POINTER;
			*object = nullptr;
			if (outer != nullptr)
				return CLASS_E_NOAGGREGATION;
			try
			{
				FakeAsioDriver* driver = new FakeAsioDriver();
				const HRESULT hr = driver->QueryInterface(riid, object);
				driver->Release();
				return hr;
			}
			catch (const std::bad_alloc&)
			{
				return E_OUTOFMEMORY;
			}
		}

		HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
		{
			if (lock)
				InterlockedIncrement(&factoryLocks);
			else
				InterlockedDecrement(&factoryLocks);
			return S_OK;
		}

	private:
		LONG refCount_ = 1;
	};
}

// cppcheck-suppress constParameterPointer ; the signature is fixed by the Win32 DllMain contract
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
	if (reason == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(instance);
	return TRUE;
}

STDAPI DllCanUnloadNow()
{
	return (FakeAsioDriver::instanceCount() == 0 && factoryLocks == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(const CLSID& clsid, const IID& iid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	*object = nullptr;
	if (clsid != CLSID_FakeAsio)
		return CLASS_E_CLASSNOTAVAILABLE;
	try
	{
		FakeClassFactory* factory = new FakeClassFactory();
		const HRESULT hr = factory->QueryInterface(iid, object);
		factory->Release();
		return hr;
	}
	catch (const std::bad_alloc&)
	{
		return E_OUTOFMEMORY;
	}
}
