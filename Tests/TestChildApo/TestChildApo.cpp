/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Real child-effect fixture for the direct-load suite and the CI audio-engine
	capture chain (audit #467 TD-26). No DllRegisterServer is provided: the CI
	gate writes the CLSID key itself. Local tests use only LoadLibrary and the
	class factory, never COM registration or an endpoint property store.
*/

#include <windows.h>
#include <BaseAudioProcessingObject.h>
#include <cmath>
#include <new>
#include "TestChildApoProtocol.h"

namespace
{
LONG objectCount = 0;
LONG serverLocks = 0;

// Matching channels, rate and sample depth, with one connection on each side.
// The DSP supports aliased input/output buffers, hence the additional INPLACE.
const CRegAPOProperties<1> properties(kTestChildApoClsid, L"Test child APO",
	L"Copyright (C) 2026", 1, 0, __uuidof(IAudioProcessingObject),
	static_cast<APO_FLAG>(APO_FLAG_DEFAULT | APO_FLAG_INPLACE));

bool supportedFormat(const UNCOMPRESSEDAUDIOFORMAT& format)
{
	return format.guidFormatType == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
		&& (format.dwBytesPerSampleContainer == 4 || format.dwBytesPerSampleContainer == 8)
		&& format.dwValidBitsPerSample == format.dwBytesPerSampleContainer * 8
		&& format.dwSamplesPerFrame != 0
		&& std::isfinite(format.fFramesPerSecond) && format.fFramesPerSecond > 0;
}

HRESULT matchingFormat(IAudioMediaType* opposite, IAudioMediaType* requested,
	IAudioMediaType** supported)
{
	if (supported != nullptr)
		*supported = nullptr;
	if (requested == nullptr)
		return E_POINTER;
	UNCOMPRESSEDAUDIOFORMAT format = {};
	HRESULT hr = requested->GetUncompressedAudioFormat(&format);
	if (FAILED(hr))
		return hr;
	if (!supportedFormat(format))
		return APOERR_FORMAT_NOT_SUPPORTED;
	if (opposite != nullptr)
	{
		UNCOMPRESSEDAUDIOFORMAT other = {};
		hr = opposite->GetUncompressedAudioFormat(&other);
		if (FAILED(hr))
			return hr;
		if (format.guidFormatType != other.guidFormatType
			|| format.dwSamplesPerFrame != other.dwSamplesPerFrame
			|| format.dwBytesPerSampleContainer != other.dwBytesPerSampleContainer
			|| format.dwValidBitsPerSample != other.dwValidBitsPerSample
			|| format.fFramesPerSecond != other.fFramesPerSecond
			|| format.dwChannelMask != other.dwChannelMask)
			return APOERR_FORMAT_NOT_SUPPORTED;
	}
	if (supported != nullptr)
	{
		requested->AddRef();
		*supported = requested;
	}
	return S_OK;
}

class TestChildApo final : public CBaseAudioProcessingObject, public IAudioSystemEffects
{
public:
	TestChildApo() : CBaseAudioProcessingObject(properties), gain(std::pow(10.0, kTestChildApoGainDb / 20.0))
	{
		InterlockedIncrement(&objectCount);
	}

	~TestChildApo() override
	{
		InterlockedDecrement(&objectCount);
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
	{
		if (object == nullptr)
			return E_POINTER;
		*object = nullptr;
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioProcessingObject))
			*object = static_cast<IAudioProcessingObject*>(this);
		else if (iid == __uuidof(IAudioProcessingObjectRT))
			*object = static_cast<IAudioProcessingObjectRT*>(this);
		else if (iid == __uuidof(IAudioProcessingObjectConfiguration))
			*object = static_cast<IAudioProcessingObjectConfiguration*>(this);
		else if (iid == __uuidof(IAudioSystemEffects))
			*object = static_cast<IAudioSystemEffects*>(this);
		if (*object == nullptr)
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG remaining = InterlockedDecrement(&references);
		if (remaining == 0)
			delete this;
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE Initialize(UINT32 size, BYTE* data) override
	{
		if (m_bIsInitialized)
			return APOERR_ALREADY_INITIALIZED;
		if ((size == 0) != (data == nullptr))
			return E_INVALIDARG;
		if (size != 0 && size < sizeof(APOInitSystemEffects))
			return E_INVALIDARG;
		// The product forwards its own initialization blob and CLSID unchanged.
		// We need no store or mode state, so never dereference its optional pointers.
		m_bIsInitialized = true;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* output,
		IAudioMediaType* requested, IAudioMediaType** supported) override
	{
		return matchingFormat(output, requested, supported);
	}

	HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* input,
		IAudioMediaType* requested, IAudioMediaType** supported) override
	{
		return matchingFormat(input, requested, supported);
	}

	HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 inputCount, APO_CONNECTION_DESCRIPTOR** inputs,
		UINT32 outputCount, APO_CONNECTION_DESCRIPTOR** outputs) override
	{
		const HRESULT hr = CBaseAudioProcessingObject::LockForProcess(inputCount, inputs, outputCount, outputs);
		if (SUCCEEDED(hr))
		{
			bytesPerSample = GetBytesPerSampleContainer();
			channels = GetSamplesPerFrame();
		}
		return hr;
	}

#pragma AVRT_CODE_BEGIN
	void STDMETHODCALLTYPE APOProcess(UINT32 inputCount, APO_CONNECTION_PROPERTY** inputs,
		UINT32 outputCount, APO_CONNECTION_PROPERTY** outputs) override
	{
		if (!m_bIsLocked || inputCount != 1 || outputCount != 1
			|| inputs == nullptr || outputs == nullptr || inputs[0] == nullptr || outputs[0] == nullptr)
			return;
		const APO_CONNECTION_PROPERTY& input = *inputs[0];
		APO_CONNECTION_PROPERTY& output = *outputs[0];
		if (input.u32BufferFlags == BUFFER_VALID)
		{
			const size_t samples = static_cast<size_t>(input.u32ValidFrameCount) * channels;
			if (bytesPerSample == sizeof(float))
				applyGain(reinterpret_cast<const float*>(input.pBuffer), reinterpret_cast<float*>(output.pBuffer), samples);
			else
				applyGain(reinterpret_cast<const double*>(input.pBuffer), reinterpret_cast<double*>(output.pBuffer), samples);
		}
		output.u32ValidFrameCount = input.u32ValidFrameCount;
		output.u32BufferFlags = input.u32BufferFlags;
	}
#pragma AVRT_CODE_END

protected:
	HRESULT __fastcall ValidateDefaultAPOFormat(UNCOMPRESSEDAUDIOFORMAT& format, bool) override
	{
		// The SDK default is float32 only; both negotiation and locking must accept
		// float64 for testRealChildFloat64, not silently process it as float32.
		return supportedFormat(format) ? S_OK : APOERR_FORMAT_NOT_SUPPORTED;
	}

	HRESULT ValidateAndCacheConnectionInfo(UINT32 inputCount, APO_CONNECTION_DESCRIPTOR** inputs,
		UINT32 outputCount, APO_CONNECTION_DESCRIPTOR** outputs) override
	{
		if (inputCount != 1 || outputCount != 1 || inputs == nullptr || outputs == nullptr
			|| inputs[0] == nullptr || outputs[0] == nullptr)
			return E_INVALIDARG;
		const HRESULT hr = matchingFormat(outputs[0]->pFormat, inputs[0]->pFormat, nullptr);
		if (FAILED(hr))
			return hr;
		return CBaseAudioProcessingObject::ValidateAndCacheConnectionInfo(inputCount, inputs, outputCount, outputs);
	}

private:
#pragma AVRT_CODE_BEGIN
	template<typename Sample>
	void applyGain(const Sample* input, Sample* output, size_t samples) const
	{
		for (size_t i = 0; i < samples; ++i)
			output[i] = static_cast<Sample>(input[i] * gain);
	}
#pragma AVRT_CODE_END

	LONG references = 1;
	UINT32 bytesPerSample = 0;
	UINT32 channels = 0;
	const double gain;
};

class ClassFactory final : public IClassFactory
{
public:
	ClassFactory() { InterlockedIncrement(&objectCount); }

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
	{
		if (object == nullptr)
			return E_POINTER;
		*object = nullptr;
		if (iid != __uuidof(IUnknown) && iid != __uuidof(IClassFactory))
			return E_NOINTERFACE;
		*object = static_cast<IClassFactory*>(this);
		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG remaining = InterlockedDecrement(&references);
		if (remaining == 0)
			delete this;
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** object) override
	{
		if (object == nullptr)
			return E_POINTER;
		*object = nullptr;
		if (outer != nullptr)
			return CLASS_E_NOAGGREGATION;
		try
		{
			auto* apo = new TestChildApo();
			const HRESULT hr = apo->QueryInterface(iid, object);
			apo->Release();
			return hr;
		}
		catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
		catch (...) { return E_FAIL; }
	}

	HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
	{
		if (lock)
			InterlockedIncrement(&serverLocks);
		else
			InterlockedDecrement(&serverLocks);
		return S_OK;
	}

private:
	~ClassFactory() { InterlockedDecrement(&objectCount); }
	LONG references = 1;
};
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	*object = nullptr;
	if (clsid != kTestChildApoClsid)
		return CLASS_E_CLASSNOTAVAILABLE;
	auto* factory = new (std::nothrow) ClassFactory();
	if (factory == nullptr)
		return E_OUTOFMEMORY;
	const HRESULT hr = factory->QueryInterface(iid, object);
	factory->Release();
	return hr;
}

extern "C" HRESULT WINAPI DllCanUnloadNow()
{
	return InterlockedCompareExchange(&objectCount, 0, 0) == 0
		&& InterlockedCompareExchange(&serverLocks, 0, 0) == 0 ? S_OK : S_FALSE;
}
