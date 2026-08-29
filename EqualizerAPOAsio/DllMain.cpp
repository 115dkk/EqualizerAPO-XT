/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	COM entry points of the ASIO wrapper DLL. A DAW activates one of the
	wrapper CLSIDs the DeviceSelector registered (one per target driver);
	DllGetClassObject looks the CLSID up in HKLM\SOFTWARE\EqualizerAPO\ASIO,
	loads the target driver it names and hands out an AsioWrapper over it.
	Nothing here touches the engine host: that happens in createBuffers,
	never during enumeration.

	EapoAsioCreateWrapper is the probe's door: it builds a wrapper over a
	target the caller already holds, from options given as text, with no
	registry involved. It is how CI exercises this DLL without registering
	anything on the runner.
*/

#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "asio/AsioWrapper.h"
#include "asio/DaemonProcessor.h"
#include "asio/WasapiExclusiveTarget.h"
#include "asio/WasapiExclusiveTarget.h"
#include "asio/Win32HostLink.h"
#include "asio/WrapperRecord.h"
#include "platform/windows/GuidText.h"
#include "services/logging/Logging.h"
#include "services/registry/RegistryError.h"
#include "services/registry/WindowsRegistry.h"

using eapo::asio::AsioWrapper;
using eapo::asio::IStreamProcessor;
using eapo::asio::Mode;
using eapo::asio::PassthroughProcessor;
using eapo::asio::StreamOptions;
using eapo::asio::WrapperRecord;

namespace
{
	HINSTANCE moduleHandle = nullptr;
	long factoryLocks = 0;
	bool loggingReady = false;

	void ensureLogging()
	{
		if (loggingReady)
			return;
		loggingReady = true;
		Logging::useUserFile(L"EqualizerAPOAsio.log", false, false, false);
	}

	// The processor a registered wrapper runs: the engine host over the
	// ring, or a plain passthrough when neither direction is processed.
	std::unique_ptr<IStreamProcessor> makeProcessor(const StreamOptions& options)
	{
		if (!options.processOutput && !options.processInput)
			return std::make_unique<PassthroughProcessor>();
		return std::make_unique<eapo::asio::DaemonProcessor>(std::make_unique<eapo::asio::Win32HostLink>());
	}

	class WrapperClassFactory final : public IClassFactory
	{
	public:
		explicit WrapperClassFactory(const CLSID& clsid, WrapperRecord record)
			: clsid_(clsid), record_(std::move(record))
		{
		}

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

			IASIO* target = nullptr;
			HRESULT hr = S_OK;
			if (record_.targetKind == eapo::asio::TargetKind::WasapiExclusive)
			{
				// The endpoints are opened in init(), when the host asks; a
				// GUID that names nothing surfaces there as the driver's
				// error message, not as a failed activation.
				try
				{
					target = new eapo::asio::WasapiExclusiveTarget(record_.renderEndpoint, record_.captureEndpoint);
				}
				catch (const std::bad_alloc&)
				{
					return E_OUTOFMEMORY;
				}
			}
			else
			{
				CLSID targetClsid;
				if (FAILED(CLSIDFromString(record_.targetClsid.c_str(), &targetClsid)))
				{
					LogFStatic(L"ASIO wrapper %s: target CLSID %s is not a GUID", record_.wrapperClsid.c_str(), record_.targetClsid.c_str());
					return E_FAIL;
				}

				// ASIO's activation quirk: the requested interface id is the
				// driver's own CLSID.
				hr = CoCreateInstance(targetClsid, nullptr, CLSCTX_INPROC_SERVER, targetClsid, reinterpret_cast<void**>(&target));
				if (FAILED(hr) || target == nullptr)
				{
					LogFStatic(L"ASIO wrapper %s: loading target %s (%s) failed with 0x%08x",
						record_.wrapperClsid.c_str(), record_.targetName.c_str(), record_.targetClsid.c_str(), static_cast<unsigned>(hr));
					return FAILED(hr) ? hr : E_FAIL;
				}
			}

			try
			{
				AsioWrapper* wrapper = new AsioWrapper(target, clsid_, record_.targetClsid, record_.options, makeProcessor(record_.options));
				target->Release();
				hr = wrapper->QueryInterface(riid, object);
				wrapper->Release();
				return hr;
			}
			catch (const std::bad_alloc&)
			{
				target->Release();
				return E_OUTOFMEMORY;
			}
			catch (...)
			{
				target->Release();
				return E_FAIL;
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
		CLSID clsid_;
		WrapperRecord record_;
	};

	// "output=1;input=0;mode=pipelined;deadline=250;ready=5000;config=C:\x\config.txt"
	StreamOptions parseOptions(const wchar_t* text)
	{
		StreamOptions options;
		if (text == nullptr)
			return options;
		std::wstring remaining(text);
		while (!remaining.empty())
		{
			const size_t separator = remaining.find(L';');
			std::wstring item = remaining.substr(0, separator);
			remaining = separator == std::wstring::npos ? L"" : remaining.substr(separator + 1);
			const size_t equals = item.find(L'=');
			if (equals == std::wstring::npos)
				continue;
			const std::wstring key = item.substr(0, equals);
			const std::wstring value = item.substr(equals + 1);
			if (key == L"output")
				options.processOutput = value == L"1";
			else if (key == L"input")
				options.processInput = value == L"1";
			else if (key == L"mode")
				options.mode = value == L"pipelined" ? Mode::Pipelined : Mode::Sync;
			else if (key == L"deadline")
				options.deadlineUs = static_cast<uint32_t>(std::wcstoul(value.c_str(), nullptr, 10));
			else if (key == L"deadlinepercent")
				options.deadlinePercent = static_cast<uint32_t>(std::wcstoul(value.c_str(), nullptr, 10));
			else if (key == L"ready")
				options.readyTimeoutMs = static_cast<uint32_t>(std::wcstoul(value.c_str(), nullptr, 10));
			else if (key == L"linger")
				options.lingerMs = static_cast<uint32_t>(std::wcstoul(value.c_str(), nullptr, 10));
			else if (key == L"config")
				options.configPath = value;
			else if (key == L"daemon")
				options.daemonExePath = value;
			else if (key == L"endpoint")
				options.daemonEndpoint = value;
		}
		return options;
	}

	// {5C2B9E10-8D4F-4A7B-B3E6-0F1A2C3D4E5F}: the CLSID a probe-created
	// wrapper answers QueryInterface with.
	constexpr GUID probeWrapperClsid = {0x5c2b9e10, 0x8d4f, 0x4a7b, {0xb3, 0xe6, 0x0f, 0x1a, 0x2c, 0x3d, 0x4e, 0x5f}};
}

// cppcheck-suppress constParameterPointer ; the signature is fixed by the Win32 DllMain contract
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		moduleHandle = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

STDAPI DllCanUnloadNow()
{
	return (AsioWrapper::instanceCount() == 0 && factoryLocks == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(const CLSID& clsid, const IID& iid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	*object = nullptr;
	ensureLogging();

	WrapperRecord record;
	try
	{
		if (!eapo::asio::WrapperRecords::read(systemRegistry(), winutil::guidToString(clsid), record))
			return CLASS_E_CLASSNOTAVAILABLE;
	}
	catch (const RegistryError&)
	{
		LogFStatic(L"ASIO wrapper: the record for %s could not be read", winutil::guidToString(clsid).c_str());
		return CLASS_E_CLASSNOTAVAILABLE;
	}
	catch (...)
	{
		return E_FAIL;
	}

	try
	{
		WrapperClassFactory* factory = new WrapperClassFactory(clsid, std::move(record));
		const HRESULT hr = factory->QueryInterface(iid, object);
		factory->Release();
		return hr;
	}
	catch (const std::bad_alloc&)
	{
		return E_OUTOFMEMORY;
	}
	catch (...)
	{
		return E_FAIL;
	}
}

// Probe entry: no registry, no COM activation. The target is the caller's;
// it is AddRef'd by the wrapper. `processorKind` selects among the adapters
// the DLL carries: "passthrough" or "daemon" (the engine host over the ring,
// reached or started as the options say).
extern "C" HRESULT __stdcall EapoAsioCreateWrapper(IASIO* target, const wchar_t* targetClsid,
	const wchar_t* optionsText, const wchar_t* processorKind, IASIO** wrapper)
{
	if (target == nullptr || wrapper == nullptr)
		return E_POINTER;
	*wrapper = nullptr;
	ensureLogging();
	try
	{
		StreamOptions options = parseOptions(optionsText);
		std::unique_ptr<IStreamProcessor> processor;
		if (processorKind == nullptr || std::wcscmp(processorKind, L"passthrough") == 0)
			processor = std::make_unique<PassthroughProcessor>();
		else if (std::wcscmp(processorKind, L"daemon") == 0)
			processor = std::make_unique<eapo::asio::DaemonProcessor>(std::make_unique<eapo::asio::Win32HostLink>());
		else
			return E_INVALIDARG;
		AsioWrapper* created = new AsioWrapper(target, probeWrapperClsid,
			targetClsid != nullptr ? targetClsid : L"", std::move(options), std::move(processor));
		*wrapper = created;
		return S_OK;
	}
	catch (const std::bad_alloc&)
	{
		return E_OUTOFMEMORY;
	}
	catch (...)
	{
		return E_FAIL;
	}
}
