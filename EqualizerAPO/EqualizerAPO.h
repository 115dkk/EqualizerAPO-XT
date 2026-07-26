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

#pragma once

#include <string>
#include <Unknwn.h>
#include <audioenginebaseapo.h>
#include <BaseAudioProcessingObject.h>
#include "../engine/FilterEngine.h"

// Identifies the single, user-configurable processing effect that
// EqualizerAPO represents. Reported through IAudioSystemEffects2::GetEffectsList
// so the audio engine and the Windows Settings "audio enhancements" surface can
// see that an effect is present on the endpoint. It is a stable private
// identifier for this APO, not one of the well-known AUDIO_EFFECT_TYPE GUIDs.
const GUID EQUALIZERAPO_EFFECT_GUID = {0x9d2e7b14, 0x6c3a, 0x4f8d, {0xa1, 0x5e, 0x2b, 0x7c, 0x9f, 0x4d, 0x8e, 0x60}};

// The hand-written reference counting (InterlockedIncrement/Decrement +
// "delete this") and this INonDelegatingUnknown interface implement COM
// aggregation: the audio engine can aggregate this APO, in which case the
// outer IUnknown (pUnkOuter) owns identity/lifetime and the inner object only
// exposes itself through the non-delegating variants. This is intentional and
// required, not a leftover. It is deliberately NOT replaced with ATL
// CComCoClass/CComAggObject: that would rewrite the COM identity of a component
// that lives inside audiodg.exe, where a subtle aggregation regression breaks
// APO loading for every user and cannot be covered by automated tests.
class INonDelegatingUnknown
{
	virtual HRESULT __stdcall NonDelegatingQueryInterface(const IID& iid, void** ppv) = 0;
	virtual ULONG __stdcall NonDelegatingAddRef() = 0;
	virtual ULONG __stdcall NonDelegatingRelease() = 0;
};

class EqualizerAPO : public CBaseAudioProcessingObject, public IAudioSystemEffects2, public INonDelegatingUnknown
{
public:
	EqualizerAPO(IUnknown* pUnkOuter);
	virtual ~EqualizerAPO();

	// IUnknown
	virtual HRESULT __stdcall QueryInterface(const IID& iid, void** ppv);
	virtual ULONG __stdcall AddRef();
	virtual ULONG __stdcall Release();

	// IAudioProcessingObject
	virtual HRESULT __stdcall GetLatency(HNSTIME* pTime);
	virtual HRESULT __stdcall Initialize(UINT32 cbDataSize, BYTE* pbyData);
	virtual HRESULT __stdcall IsInputFormatSupported(IAudioMediaType* pOutputFormat,
		IAudioMediaType* pRequestedInputFormat, IAudioMediaType** ppSupportedInputFormat);

	// IAudioProcessingObjectConfiguration
	virtual HRESULT __stdcall LockForProcess(UINT32 u32NumInputConnections,
		APO_CONNECTION_DESCRIPTOR** ppInputConnections, UINT32 u32NumOutputConnections,
		APO_CONNECTION_DESCRIPTOR** ppOutputConnections);
	virtual HRESULT __stdcall UnlockForProcess(void);

	// IAudioProcessingObjectRT
	virtual void __stdcall APOProcess(UINT32 u32NumInputConnections,
		APO_CONNECTION_PROPERTY** ppInputConnections, UINT32 u32NumOutputConnections,
		APO_CONNECTION_PROPERTY** ppOutputConnections);

	// IAudioSystemEffects2 (inherits the marker IAudioSystemEffects)
	virtual HRESULT __stdcall GetEffectsList(LPGUID* effects, UINT* numEffects, HANDLE event);

	// INonDelegatingUnknown
	HRESULT __stdcall NonDelegatingQueryInterface(const IID& iid, void** ppv) override;
	ULONG __stdcall NonDelegatingAddRef() override;
	ULONG __stdcall NonDelegatingRelease() override;

	static long instCount;
	static const CRegAPOProperties<1> regPostMixProperties;
	static const CRegAPOProperties<1> regPreMixProperties;

	enum class ApoSampleFormat
	{
		Unsupported = 0,
		Float32 = 1,
		Float64 = 2
	};

private:
	long refCount;
	IUnknown* pUnkOuter;
	FilterEngine engine;
	bool allowSilentBufferModification;

	void resetChild();
	void sendMessage(std::wstring& deviceTestPipeName, const std::wstring& deviceGuid, GUID apoGuid, const std::string& phase);

	IAudioProcessingObject* childAPO;
	IAudioProcessingObjectRT* childRT;
	IAudioProcessingObjectConfiguration* childCfg;

	// Resolved during LockForProcess from the connection format. The APO is
	// registered with APO_FLAG_BITSPERSAMPLE_MUST_MATCH so the two sides always
	// agree on bit depth in normal operation, but both are stored for clarity
	// and to keep silent-fast-path math correct.
	ApoSampleFormat inputSampleFormat;
	ApoSampleFormat outputSampleFormat;
};
