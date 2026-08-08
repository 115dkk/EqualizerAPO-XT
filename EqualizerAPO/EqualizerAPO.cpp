/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2012  Jonas Thedering

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
#include <exception>
#include <new>
#include <Unknwn.h>
#define INITGUID
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ksmedia.h>

#include "../helpers/LogHelper.h"
#include "../helpers/ComBoundary.h"
#include "../helpers/RegistryHelper.h"
#include "../helpers/StringHelper.h"
#include "../helpers/ComPtr.h"
#include "../helpers/Win32Resource.h"
#include "../devices/DeviceAPOInfo.h"
#include "../devices/DeviceAPOInfoKeys.h"
#include "EqualizerAPO.h"

namespace
{
	EqualizerAPO::ApoSampleFormat detectSampleFormat(const UNCOMPRESSEDAUDIOFORMAT& f)
	{
		// Windows audio engine normally hands system-effect APOs IEEE_FLOAT samples
		// even when the endpoint runs an integer format underneath. We only need to
		// match the container size to pick the right reinterpret. Validating the
		// exact dwValidBitsPerSample value is too strict — some virtual devices
		// (CABLE Input, loopback adapters, etc.) report non-canonical valid-bit
		// counts even though the container is plain 32-bit float. Rejecting them
		// here would fall through to silent output and make the device sound
		// dead.
		if (IsEqualGUID(f.guidFormatType, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
		{
			if (f.dwBytesPerSampleContainer == 4)
				return EqualizerAPO::ApoSampleFormat::Float32;
			if (f.dwBytesPerSampleContainer == 8)
				return EqualizerAPO::ApoSampleFormat::Float64;
		}
		return EqualizerAPO::ApoSampleFormat::Unsupported;
	}

	size_t bytesPerSample(EqualizerAPO::ApoSampleFormat fmt)
	{
		switch (fmt)
		{
		case EqualizerAPO::ApoSampleFormat::Float32: return sizeof(float);
		case EqualizerAPO::ApoSampleFormat::Float64: return sizeof(double);
		default: return 0;
		}
	}

	// Shared block processing parameterized on the connection sample type. The
	// FilterEngine has overloads for both float* and double* so the same body
	// works for either path. APO_FLAG_BITSPERSAMPLE_MUST_MATCH guarantees the
	// input and output sides share the same SampleT.
	template<typename SampleT>
	inline void processBlock(
		SampleT* inputFrames, SampleT* outputFrames,
		unsigned frameCount,
		unsigned inputChannelCount, unsigned outputChannelCount,
		bool isSilentInput, bool allowSilentBufferModification,
		IAudioProcessingObjectRT* childRT, FilterEngine& engine,
		UINT32 u32NumInputConnections, APO_CONNECTION_PROPERTY** ppInputConnections,
		UINT32 u32NumOutputConnections, APO_CONNECTION_PROPERTY** ppOutputConnections)
	{
		if (isSilentInput)
			std::fill_n(inputFrames, frameCount * inputChannelCount, static_cast<SampleT>(0));

		if (childRT)
		{
			childRT->APOProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections, ppOutputConnections);
			engine.process(outputFrames, outputFrames, frameCount);
		}
		else
		{
			engine.process(outputFrames, inputFrames, frameCount);
		}

		ppOutputConnections[0]->u32ValidFrameCount = frameCount;

		if (isSilentInput)
		{
			if (allowSilentBufferModification)
			{
				unsigned outputSampleCount = frameCount * outputChannelCount;
				bool silent = true;
				const SampleT threshold = static_cast<SampleT>(1e-10);
				for (unsigned i = 0; i < outputSampleCount; i++)
				{
					if (std::abs(outputFrames[i]) > threshold)
					{
						silent = false;
						break;
					}
				}
				// BUFFER_SILENT seems to be important for some sound card drivers,
				// so only use BUFFER_VALID if there really is audio.
				ppOutputConnections[0]->u32BufferFlags = silent ? BUFFER_SILENT : BUFFER_VALID;
			}
			else
			{
				std::fill_n(outputFrames, frameCount * outputChannelCount, static_cast<SampleT>(0));
				ppOutputConnections[0]->u32BufferFlags = BUFFER_SILENT;
			}
		}
		else
		{
			ppOutputConnections[0]->u32BufferFlags = BUFFER_VALID;
		}
	}
}

using std::abs;
using std::string;
using std::wstring;

long EqualizerAPO::instCount = 0;
const CRegAPOProperties<1> EqualizerAPO::regPostMixProperties(
	EQUALIZERAPO_POST_MIX_GUID, L"EqualizerAPO", L"Copyright (C) 2015", 1, 0, __uuidof(IAudioProcessingObject),
	(APO_FLAG)(APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE));
const CRegAPOProperties<1> EqualizerAPO::regPreMixProperties(
	EQUALIZERAPO_PRE_MIX_GUID, L"EqualizerAPO", L"Copyright (C) 2015", 1, 0, __uuidof(IAudioProcessingObject),
	(APO_FLAG)(APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE));

EqualizerAPO::EqualizerAPO(IUnknown* pUnkOuter)
	: CBaseAudioProcessingObject(regPostMixProperties)
{
	refCount = 1;
	if (pUnkOuter != nullptr)
		this->pUnkOuter = pUnkOuter;
	else
		this->pUnkOuter = reinterpret_cast<IUnknown*>(static_cast<INonDelegatingUnknown*>(this));

	allowSilentBufferModification = false;

	childAPO = nullptr;
	childRT = nullptr;
	childCfg = nullptr;

	inputSampleFormat = ApoSampleFormat::Unsupported;
	outputSampleFormat = ApoSampleFormat::Unsupported;

	InterlockedIncrement(&instCount);
}

EqualizerAPO::~EqualizerAPO()
{
	InterlockedDecrement(&instCount);

	resetChild();
}

HRESULT EqualizerAPO::QueryInterface(const IID& iid, void** ppv)
{
	return pUnkOuter->QueryInterface(iid, ppv);
}

ULONG EqualizerAPO::AddRef()
{
	return pUnkOuter->AddRef();
}

ULONG EqualizerAPO::Release()
{
	return pUnkOuter->Release();
}

HRESULT EqualizerAPO::GetLatency(HNSTIME* pTime)
{
	if (!pTime)
		return E_POINTER;

	if (!m_bIsLocked)
		return APOERR_ALREADY_UNLOCKED;

	*pTime = 0;
	if (childAPO)
		childAPO->GetLatency(pTime);

	return S_OK;
}

HRESULT EqualizerAPO::Initialize(UINT32 cbDataSize, BYTE* pbyData)
{
	return ComBoundary::invoke([&]() -> HRESULT {
	LogHelper::reset();

	TraceF(L"Initialize: cbDataSize=%u (APOInitSystemEffects=%u)", cbDataSize, static_cast<unsigned>(sizeof(APOInitSystemEffects)));

	if ((nullptr == pbyData) && (0 != cbDataSize))
		return E_INVALIDARG;
	if ((nullptr != pbyData) && (0 == cbDataSize))
		return E_POINTER;
	// Since 64970cd this APO exposes IAudioSystemEffects2, so the audio engine
	// hands Initialize the larger APOInitSystemEffects2 struct (and would hand a
	// future IAudioSystemEffects3 build an APOInitSystemEffects3) rather than the
	// base APOInitSystemEffects. Demanding the exact v1 size rejected every modern
	// init with E_INVALIDARG, so the effect never actually loaded - the device
	// test reported "Initialize failed ... (the parameter is incorrect)" and the
	// EQ was silently inactive on endpoints whose effect slot does not tolerate a
	// failing APO. Every APOInitSystemEffects* version shares the same leading
	// members (APOInit, pAPOEndpointProperties, ...), so accept any struct at
	// least the base size and keep reading those common fields below.
	if (cbDataSize < sizeof(APOInitSystemEffects))
		return E_INVALIDARG;

	resetChild();

	APOInitSystemEffects* initStruct = reinterpret_cast<APOInitSystemEffects*>(pbyData);
	GUID apoGuid = initStruct->APOInit.clsid;
	try
	{
		TraceF(L"APO GUID: %s", RegistryHelper::getGuidString(apoGuid).c_str());
	}
	catch (const RegistryException&)
	{
		LogF(L"Could not convert apo guid to guid string");
	}
	engineSetup.preMix = (apoGuid == EQUALIZERAPO_PRE_MIX_GUID) != 0;

	winutil::PropVariant var;
	HRESULT hr = initStruct->pAPOEndpointProperties->GetValue(PKEY_AudioEndpoint_GUID, &var);
	if (FAILED(hr))
	{
		LogF(L"Can't read endpoint guid");
		return hr;
	}
	if (var->vt != VT_LPWSTR || var->pwszVal == nullptr)
	{
		LogF(L"Endpoint guid property has unexpected type");
		return E_UNEXPECTED;
	}
	wstring deviceGuid = var->pwszVal;
	TraceF(L"Endpoint GUID: %s", deviceGuid.c_str());

	wstring deviceTestPipeName;
	try
	{
		// Audit #250 F020: the pipe name is shared vocabulary (DeviceAPOInfoKeys.h).
		if (RegistryHelper::valueExists(APP_REGPATH, deviceTestPipeValueName))
			deviceTestPipeName = RegistryHelper::readValue(APP_REGPATH, deviceTestPipeValueName);
	}
	catch (const RegistryException& e)
	{
		LogF(L"%s", e.getMessage().c_str());
	}

	if (deviceTestPipeName != L"")
		sendMessage(deviceTestPipeName, deviceGuid, apoGuid, "Initialize");

	wstring childApoGuid;

	try
	{
		DeviceAPOInfo apoInfo;
		if (apoInfo.load(deviceGuid))
		{
			engineSetup.capture = apoInfo.isInput();
			engineSetup.postMixInstalled = apoInfo.getCurrentInstallState().installPostMix;
			engineSetup.deviceName = apoInfo.getDeviceName();
			engineSetup.connectionName = apoInfo.getConnectionName();
			engineSetup.deviceGuid = apoInfo.getDeviceGuid();

			if (apoGuid == EQUALIZERAPO_PRE_MIX_GUID)
				childApoGuid = apoInfo.getPreMixChildGuid();
			else
				childApoGuid = apoInfo.getPostMixChildGuid();

			allowSilentBufferModification = apoInfo.getCurrentInstallState().allowSilentBufferModification;
		}
	}
	catch (const RegistryException& e)
	{
		LogF(L"Could not read endpoint device info because of: %s", e.getMessage().c_str());
	}

	TraceF(L"Child APO GUID: %s", childApoGuid.c_str());

	if (childApoGuid != L"" && childApoGuid != APOGUID_NULL && childApoGuid != APOGUID_NOKEY && childApoGuid != APOGUID_NOVALUE)
	{
		GUID childGuid;
		hr = CLSIDFromString(childApoGuid.c_str(), &childGuid);
		if (FAILED(hr))
		{
			LogF(L"Can't convert child apo guid string to guid");
			return S_OK;
		}

		hr = CoCreateInstance(childGuid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IAudioProcessingObject), (void**)&childAPO);
		if (FAILED(hr))
		{
			LogF(L"Error in CoCreateInstance for child apo");
			resetChild();
			return S_OK;
		}

		hr = childAPO->QueryInterface(__uuidof(IAudioProcessingObjectRT), (void**)&childRT);
		if (FAILED(hr))
		{
			LogF(L"Error in QueryInterface for child apo RT");
			resetChild();
			return S_OK;
		}

		hr = childAPO->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), (void**)&childCfg);
		if (FAILED(hr))
		{
			LogF(L"Error in QueryInterface for child apo configuration");
			resetChild();
			return S_OK;
		}

		hr = childAPO->Initialize(cbDataSize, pbyData);
		if (FAILED(hr))
		{
			LogF(L"Error in Initialize of child apo");
			resetChild();
			return S_OK;
		}

		TraceF(L"Successfully created and initialized child APO");

		if (deviceTestPipeName != L"")
			sendMessage(deviceTestPipeName, deviceGuid, apoGuid, "ChildAPO");
	}

	return S_OK;
	});
}

HRESULT EqualizerAPO::IsInputFormatSupported(IAudioMediaType* pOutputFormat,
	IAudioMediaType* pRequestedInputFormat, IAudioMediaType** ppSupportedInputFormat)
{
	return ComBoundary::invoke([&]() -> HRESULT {
	// Audit #250 F028: the output format is dereferenced below just like the
	// requested input format, so it gets the same guard.
	if (!pRequestedInputFormat || !pOutputFormat)
		return E_POINTER;

	UNCOMPRESSEDAUDIOFORMAT inFormat;
	HRESULT hr = pRequestedInputFormat->GetUncompressedAudioFormat(&inFormat);
	if (FAILED(hr))
	{
		LogF(L"Error in GetUncompressedAudioFormat");
		return hr;
	}

	TraceF(L"RequestedInputFormat = { %08X, %u, %u, %u, %f, %08X }",
		inFormat.guidFormatType.Data1, inFormat.dwSamplesPerFrame, inFormat.dwBytesPerSampleContainer,
		inFormat.dwValidBitsPerSample, inFormat.fFramesPerSecond, inFormat.dwChannelMask);

	UNCOMPRESSEDAUDIOFORMAT outFormat;
	hr = pOutputFormat->GetUncompressedAudioFormat(&outFormat);
	if (FAILED(hr))
	{
		LogF(L"Error in second GetUncompressedAudioFormat");
		return hr;
	}

	TraceF(L"Output format = { %08X, %u, %u, %u, %f, %08X }",
		outFormat.guidFormatType.Data1, outFormat.dwSamplesPerFrame, outFormat.dwBytesPerSampleContainer,
		outFormat.dwValidBitsPerSample, outFormat.fFramesPerSecond, outFormat.dwChannelMask);

	if (childAPO)
	{
		hr = childAPO->IsInputFormatSupported(pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);
		if (SUCCEEDED(hr))
		{
			TraceF(L"Success in IsInputFormatSupported of child apo");
		}
		else
		{
			LogF(L"Failure in IsInputFormatSupported of child apo");
			resetChild();
		}
	}

	if (!childAPO || !SUCCEEDED(hr))
	{
		hr = CBaseAudioProcessingObject::IsInputFormatSupported(pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);

		// we do not support downmixing currently
		if (hr == S_OK && inFormat.dwSamplesPerFrame > 2 && inFormat.dwSamplesPerFrame > outFormat.dwSamplesPerFrame)
		{
			// Audit #250 F029: a failed media-type creation used to be
			// forced to S_FALSE, sending the caller to dereference an
			// unset *ppSupportedInputFormat below.
			hr = CreateAudioMediaTypeFromUncompressedAudioFormat(&outFormat, ppSupportedInputFormat);
			if (FAILED(hr))
			{
				LogF(L"Error in CreateAudioMediaTypeFromUncompressedAudioFormat");
				return hr;
			}
			hr = S_FALSE;
		}
	}

	if (hr == S_FALSE)
	{
		UNCOMPRESSEDAUDIOFORMAT supportedFormat;
		HRESULT hr2 = (*ppSupportedInputFormat)->GetUncompressedAudioFormat(&supportedFormat);
		if (FAILED(hr2))
		{
			LogF(L"Error in third GetUncompressedAudioFormat");
			return hr2;
		}

		TraceF(L"InputFormat not accepted, SupportedInputFormat = { %08X, %u, %u, %u, %f, %08X }",
			supportedFormat.guidFormatType.Data1, supportedFormat.dwSamplesPerFrame, supportedFormat.dwBytesPerSampleContainer,
			supportedFormat.dwValidBitsPerSample, supportedFormat.fFramesPerSecond, supportedFormat.dwChannelMask);
	}
	else if (hr == S_OK)
	{
		TraceF(L"InputFormat accepted");
	}

	return hr;
	});
}

HRESULT EqualizerAPO::LockForProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_DESCRIPTOR** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_DESCRIPTOR** ppOutputConnections)
{
	return ComBoundary::invoke([&]() -> HRESULT {
	HRESULT hr;

	UNCOMPRESSEDAUDIOFORMAT inFormat;
	hr = ppInputConnections[0]->pFormat->GetUncompressedAudioFormat(&inFormat);
	if (FAILED(hr))
	{
		LogF(L"Error in GetUncompressedAudioFormat in LockForProcess");
		return hr;
	}

	unsigned maxInputFrameCount = ppInputConnections[0]->u32MaxFrameCount;

	TraceF(L"Input format in LockForProcess = { %08X, %u, %u, %u, %f, %08X, %u }",
		inFormat.guidFormatType.Data1, inFormat.dwSamplesPerFrame, inFormat.dwBytesPerSampleContainer,
		inFormat.dwValidBitsPerSample, inFormat.fFramesPerSecond, inFormat.dwChannelMask, maxInputFrameCount);

	UNCOMPRESSEDAUDIOFORMAT outFormat;
	hr = ppOutputConnections[0]->pFormat->GetUncompressedAudioFormat(&outFormat);
	if (FAILED(hr))
	{
		LogF(L"Error in second GetUncompressedAudioFormat in LockForProcess");
		return hr;
	}

	unsigned maxOutputFrameCount = ppOutputConnections[0]->u32MaxFrameCount;

	TraceF(L"Output format in LockForProcess = { %08X, %u, %u, %u, %f, %08X, %u }",
		outFormat.guidFormatType.Data1, outFormat.dwSamplesPerFrame, outFormat.dwBytesPerSampleContainer,
		outFormat.dwValidBitsPerSample, outFormat.fFramesPerSecond, outFormat.dwChannelMask, maxOutputFrameCount);

	inputSampleFormat = detectSampleFormat(inFormat);
	outputSampleFormat = detectSampleFormat(outFormat);
	TraceF(L"Resolved APO sample formats: in=%d, out=%d", static_cast<int>(inputSampleFormat), static_cast<int>(outputSampleFormat));

	// Loud warning so the user can see in TraceLog.txt why a device sounds dry:
	// when the format is something we cannot natively process the engine is
	// bypassed and the input is forwarded straight to the output without any
	// filtering. This is still preferable to going silent.
	if (inputSampleFormat == ApoSampleFormat::Unsupported || outputSampleFormat == ApoSampleFormat::Unsupported)
	{
		LogF(L"Connection format is not IEEE_FLOAT 32/64 (in container=%u valid=%u, out container=%u valid=%u). "
			L"APO will passthrough audio without applying filters for this stream.",
			inFormat.dwBytesPerSampleContainer, inFormat.dwValidBitsPerSample,
			outFormat.dwBytesPerSampleContainer, outFormat.dwValidBitsPerSample);
	}

	if (childCfg != nullptr)
	{
		hr = childCfg->LockForProcess(u32NumInputConnections, ppInputConnections, u32NumOutputConnections,
			ppOutputConnections);
		if (SUCCEEDED(hr))
		{
			TraceF(L"Success in LockForProcess of child apo");
		}
		else
		{
			LogF(L"Child APO LockForProcess failed (hr=0x%08X); detaching child to avoid invoking an unlocked APO during process", hr);
			resetChild();
		}
	}

	hr = CBaseAudioProcessingObject::LockForProcess(u32NumInputConnections, ppInputConnections,
		u32NumOutputConnections, ppOutputConnections);
	if (FAILED(hr))
	{
		LogF(L"Error in CBaseAudioProcessingObject::LockForProcess");
		return hr;
	}
	else if (hr == S_OK)
	{
		TraceF(L"LockForProcess successful");
	}

	unsigned maxFrameCount = maxInputFrameCount;
	if (maxFrameCount == 0)
		maxFrameCount = maxOutputFrameCount;

	unsigned realChannelCount;
	if (childCfg != nullptr)
		realChannelCount = outFormat.dwSamplesPerFrame;
	else
		realChannelCount = inFormat.dwSamplesPerFrame;

	unsigned channelMask;
	if (engine.isCapture())
	{
		channelMask = inFormat.dwChannelMask;
		if (channelMask == 0 && inFormat.dwSamplesPerFrame == outFormat.dwSamplesPerFrame)
			channelMask = outFormat.dwChannelMask;
	}
	else
	{
		channelMask = outFormat.dwChannelMask;
		if (channelMask == 0 && inFormat.dwSamplesPerFrame == outFormat.dwSamplesPerFrame)
			channelMask = inFormat.dwChannelMask;
	}

	try
	{
		engineSetup.sampleRate = outFormat.fFramesPerSecond;
		engineSetup.inputChannelCount = inFormat.dwSamplesPerFrame;
		engineSetup.realChannelCount = realChannelCount;
		engineSetup.outputChannelCount = outFormat.dwSamplesPerFrame;
		engineSetup.channelMask = channelMask;
		engineSetup.maxFrameCount = maxFrameCount;
		engine.initialize(engineSetup);
	}
	catch (const std::bad_alloc&)
	{
		LogF(L"Filter engine initialization ran out of memory; rejecting LockForProcess instead of terminating the audio service");
		UnlockForProcess();
		return E_OUTOFMEMORY;
	}
	catch (const std::exception& e)
	{
		LogF(L"Filter engine initialization failed; rejecting LockForProcess: %S", e.what());
		UnlockForProcess();
		return E_FAIL;
	}
	catch (...)
	{
		LogF(L"Filter engine initialization failed with an unknown exception; rejecting LockForProcess");
		UnlockForProcess();
		return E_FAIL;
	}

	return hr;
	});
}

HRESULT EqualizerAPO::UnlockForProcess()
{
	return ComBoundary::invoke([&]() -> HRESULT {
	if (childCfg)
	{
		HRESULT hr = childCfg->UnlockForProcess();
		if (FAILED(hr))
		{
			// Audit #250 F033: returning early here left the base APO
			// locked forever. LockForProcess detaches a failing child in
			// the symmetric situation; do the same and keep unlocking.
			LogF(L"Error in UnlockForProcess of child apo; detaching child");
			resetChild();
		}
	}

	return CBaseAudioProcessingObject::UnlockForProcess();
	});
}

void EqualizerAPO::resetChild()
{
	if (childAPO != nullptr)
	{
		childAPO->Release();
		childAPO = nullptr;
	}

	if (childRT != nullptr)
	{
		childRT->Release();
		childRT = nullptr;
	}

	if (childCfg != nullptr)
	{
		childCfg->Release();
		childCfg = nullptr;
	}
}

void EqualizerAPO::sendMessage(std::wstring& deviceTestPipeName, const std::wstring& deviceGuid, GUID apoGuid, const std::string& phase)
{
	string message = "{\"deviceGuid\":\"" + StringHelper::toString(deviceGuid, CP_UTF8) + "\", \"stage\":\"" + (apoGuid == EQUALIZERAPO_PRE_MIX_GUID ? "PreMix" : "PostMix") + "\", \"phase\":\"" + phase + "\"}";

	winutil::UniqueHandle pipe(CreateFileW((L"\\\\.\\pipe\\" + deviceTestPipeName).c_str(),
		GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
	if (!pipe)
	{
		if (WaitNamedPipeW((L"\\\\.\\pipe\\" + deviceTestPipeName).c_str(), 1000))
			pipe.reset(CreateFileW((L"\\\\.\\pipe\\" + deviceTestPipeName).c_str(),
				GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
	}
	if (pipe)
	{
		DWORD bytesWritten;
		if (!WriteFile(pipe.get(), message.c_str(), static_cast<int>(message.length()), &bytesWritten, nullptr))
			LogF(L"Could not write to pipe: %s", StringHelper::getSystemErrorString(GetLastError()).c_str());

		FlushFileBuffers(pipe.get());
	}
	else
	{
		LogF(L"Could not connect to named pipe: %s", StringHelper::getSystemErrorString(GetLastError()).c_str());
		deviceTestPipeName = L"";
	}
}

#pragma AVRT_CODE_BEGIN
void EqualizerAPO::APOProcess(UINT32 u32NumInputConnections,
	APO_CONNECTION_PROPERTY** ppInputConnections, UINT32 u32NumOutputConnections,
	APO_CONNECTION_PROPERTY** ppOutputConnections)
{
	switch (ppInputConnections[0]->u32BufferFlags)
	{
	case BUFFER_VALID:
	case BUFFER_SILENT:
	{
		const unsigned frameCount = ppInputConnections[0]->u32ValidFrameCount;
		const bool isSilentInput = (ppInputConnections[0]->u32BufferFlags == BUFFER_SILENT);
		const unsigned outputChannelCount = engine.getOutputChannelCount();
		const unsigned inputChannelCount = engine.getInputChannelCount();

		// Silent input fast path. When the active configuration has no stateful or
		// tail-bearing filter, the host does not require us to surface newly-audible
		// output (allowSilentBufferModification == false), and no child APO could
		// synthesize audio, the engine can be skipped entirely. Works for any
		// connection sample format since we only need to zero the output buffer.
		if (isSilentInput && !allowSilentBufferModification && !childRT && !engine.hasStatefulOrTailFilters())
		{
			const size_t outBytes = bytesPerSample(outputSampleFormat);
			if (outBytes > 0)
			{
				memset(reinterpret_cast<void*>(ppOutputConnections[0]->pBuffer), 0,
					static_cast<size_t>(frameCount) * outputChannelCount * outBytes);
				ppOutputConnections[0]->u32ValidFrameCount = frameCount;
				ppOutputConnections[0]->u32BufferFlags = BUFFER_SILENT;
				break;
			}
			// Fall through to normal processing if format is unknown so we don't
			// silently mis-handle an unexpected connection.
		}

		// The APO is registered with APO_FLAG_BITSPERSAMPLE_MUST_MATCH, so input
		// and output formats should agree on container size. Only take the native
		// path when both sides resolved to the same supported format — otherwise
		// fall through to the passthrough branch so we never reinterpret integer
		// samples as float.
		const bool nativePathSafe = (inputSampleFormat == outputSampleFormat)
			&& (inputSampleFormat != ApoSampleFormat::Unsupported);
		if (nativePathSafe && inputSampleFormat == ApoSampleFormat::Float64)
		{
			double* inputFrames = reinterpret_cast<double*>(ppInputConnections[0]->pBuffer);
			double* outputFrames = reinterpret_cast<double*>(ppOutputConnections[0]->pBuffer);
			processBlock<double>(inputFrames, outputFrames, frameCount,
				inputChannelCount, outputChannelCount,
				isSilentInput, allowSilentBufferModification,
				childRT, engine,
				u32NumInputConnections, ppInputConnections,
				u32NumOutputConnections, ppOutputConnections);
		}
		else if (nativePathSafe && inputSampleFormat == ApoSampleFormat::Float32)
		{
			float* inputFrames = reinterpret_cast<float*>(ppInputConnections[0]->pBuffer);
			float* outputFrames = reinterpret_cast<float*>(ppOutputConnections[0]->pBuffer);
			processBlock<float>(inputFrames, outputFrames, frameCount,
				inputChannelCount, outputChannelCount,
				isSilentInput, allowSilentBufferModification,
				childRT, engine,
				u32NumInputConnections, ppInputConnections,
				u32NumOutputConnections, ppOutputConnections);
		}
		else
		{
			// Unsupported or mismatched connection format: do NOT process, but
			// still let audio reach the device. The APO is registered with
			// APO_FLAG_INPLACE, so a conformant host hands us the same buffer
			// for input and output — the samples already sit at outBuf untouched
			// and we just have to mark the buffer valid. Emitting BUFFER_SILENT
			// here instead makes the device go mute the moment the APO is
			// installed.
			//
			// If a host does call us with distinct in/out buffers we cannot
			// safely copy the bytes through because we do not know the exact
			// input container size when the format is unsupported, and copying
			// the wrong number of bytes would either truncate the signal or
			// read past the input buffer. In that rare case we fall back to
			// silence — it is still better than emitting random memory, and
			// such a host is non-compliant given APO_FLAG_INPLACE anyway.
			const void* inBuf = reinterpret_cast<const void*>(ppInputConnections[0]->pBuffer);
			void* outBuf = reinterpret_cast<void*>(ppOutputConnections[0]->pBuffer);
			ppOutputConnections[0]->u32ValidFrameCount = frameCount;
			if (inBuf == outBuf)
			{
				ppOutputConnections[0]->u32BufferFlags = isSilentInput ? BUFFER_SILENT : BUFFER_VALID;
			}
			else
			{
				const size_t outBytes = bytesPerSample(outputSampleFormat);
				if (outBytes > 0)
				{
					memset(outBuf, 0,
						static_cast<size_t>(frameCount) * outputChannelCount * outBytes);
				}
				ppOutputConnections[0]->u32BufferFlags = BUFFER_SILENT;
			}
		}

		break;
	}
	}
}
#pragma AVRT_CODE_END

HRESULT EqualizerAPO::GetEffectsList(LPGUID* effects, UINT* numEffects, HANDLE /*event*/)
{
	return ComBoundary::invoke([&]() -> HRESULT {
	if (effects == nullptr || numEffects == nullptr)
		return E_POINTER;

	// EqualizerAPO always represents a single, user-configurable processing
	// effect. Report it so the audio engine (and the Windows Settings audio
	// enhancements surface) can see that an effect is present on the endpoint.
	// The reported set never changes at runtime, so the change-notification
	// event is intentionally not retained or signalled.
	LPGUID list = static_cast<LPGUID>(CoTaskMemAlloc(sizeof(GUID)));
	if (list == nullptr)
	{
		*effects = nullptr;
		*numEffects = 0;
		return E_OUTOFMEMORY;
	}

	list[0] = EQUALIZERAPO_EFFECT_GUID;
	*effects = list;
	*numEffects = 1;
	return S_OK;
	});
}

HRESULT EqualizerAPO::NonDelegatingQueryInterface(const IID& iid, void** ppv)
{
	if (iid == __uuidof(IUnknown))
		*ppv = static_cast<INonDelegatingUnknown*>(this);
	else if (iid == __uuidof(IAudioProcessingObject))
		*ppv = static_cast<IAudioProcessingObject*>(this);
	else if (iid == __uuidof(IAudioProcessingObjectRT))
		*ppv = static_cast<IAudioProcessingObjectRT*>(this);
	else if (iid == __uuidof(IAudioProcessingObjectConfiguration))
		*ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
	else if (iid == __uuidof(IAudioSystemEffects))
		*ppv = static_cast<IAudioSystemEffects*>(this);
	else if (iid == __uuidof(IAudioSystemEffects2))
		*ppv = static_cast<IAudioSystemEffects2*>(this);
	else
	{
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	reinterpret_cast<IUnknown*>(*ppv)->AddRef();
	return S_OK;
}

ULONG EqualizerAPO::NonDelegatingAddRef()
{
	return InterlockedIncrement(&refCount);
}

ULONG EqualizerAPO::NonDelegatingRelease()
{
	const LONG remaining = InterlockedDecrement(&refCount);
	if (remaining == 0)
	{
		delete this;
		return 0;
	}

	return static_cast<ULONG>(remaining);
}
