/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ChildApo.h"
#include "services/logging/Logging.h"

ChildApo::~ChildApo()
{
	reset();
}

HRESULT ChildApo::coCreate(REFCLSID clsid, IAudioProcessingObject** object)
{
	return CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
		__uuidof(IAudioProcessingObject), reinterpret_cast<void**>(object));
}

ChildApo::AttachResult ChildApo::attach(const std::wstring& clsidText,
	UINT32 cbDataSize, BYTE* pbyData, const Activator& activate)
{
	reset();
	if (clsidText.empty())
		return AttachResult::NoChild;

	GUID childGuid;
	HRESULT hr = CLSIDFromString(clsidText.c_str(), &childGuid);
	if (FAILED(hr))
	{
		LogF(L"Can't convert child apo guid string to guid");
		return AttachResult::BadClsid;
	}

	hr = activate(childGuid, childAPO.put());
	if (FAILED(hr))
	{
		LogF(L"Error in CoCreateInstance for child apo");
		reset();
		return AttachResult::ActivationFailed;
	}

	hr = childAPO->QueryInterface(__uuidof(IAudioProcessingObjectRT), reinterpret_cast<void**>(childRT.put()));
	if (FAILED(hr))
	{
		LogF(L"Error in QueryInterface for child apo RT");
		reset();
		return AttachResult::NoRealtimeInterface;
	}

	hr = childAPO->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), reinterpret_cast<void**>(childCfg.put()));
	if (FAILED(hr))
	{
		LogF(L"Error in QueryInterface for child apo configuration");
		reset();
		return AttachResult::NoConfigurationInterface;
	}

	hr = childAPO->Initialize(cbDataSize, pbyData);
	if (FAILED(hr))
	{
		LogF(L"Error in Initialize of child apo");
		reset();
		return AttachResult::InitializeFailed;
	}

	TraceF(L"Successfully created and initialized child APO");
	return AttachResult::Attached;
}

bool ChildApo::attached() const noexcept
{
	return childRT != nullptr;
}

HRESULT ChildApo::isInputFormatSupported(IAudioMediaType* outputFormat,
	IAudioMediaType* requestedInputFormat, IAudioMediaType** supportedInputFormat)
{
	if (!childAPO)
		return E_UNEXPECTED;

	const HRESULT hr = childAPO->IsInputFormatSupported(outputFormat, requestedInputFormat, supportedInputFormat);
	if (SUCCEEDED(hr))
	{
		TraceF(L"Success in IsInputFormatSupported of child apo");
	}
	else
	{
		LogF(L"Failure in IsInputFormatSupported of child apo");
		reset();
	}
	return hr;
}

HRESULT ChildApo::getLatency(HNSTIME* time)
{
	return childAPO ? childAPO->GetLatency(time) : E_UNEXPECTED;
}

bool ChildApo::lockForProcess(UINT32 inputCount, APO_CONNECTION_DESCRIPTOR** inputs,
	UINT32 outputCount, APO_CONNECTION_DESCRIPTOR** outputs)
{
	if (!childCfg)
		return false;

	const HRESULT hr = childCfg->LockForProcess(inputCount, inputs, outputCount, outputs);
	if (SUCCEEDED(hr))
	{
		TraceF(L"Success in LockForProcess of child apo");
		return true;
	}

	// A vendor APO that rejects locking must not receive APOProcess afterwards;
	// testChildLockFailureDetaches exercises that failure (audit #467 TD-26).
	LogF(L"Child APO LockForProcess failed (hr=0x%08X); detaching child to avoid invoking an unlocked APO during process", hr);
	reset();
	return false;
}

void ChildApo::unlockForProcess()
{
	if (childCfg)
	{
		const HRESULT hr = childCfg->UnlockForProcess();
		if (FAILED(hr))
		{
			// Audit #250 F033: returning early here left the base APO
			// locked forever. LockForProcess detaches a failing child in
			// the symmetric situation; do the same and keep unlocking.
			// A vendor APO's failed unlock is covered by
			// testChildUnlockFailureDetaches (audit #467 TD-26).
			LogF(L"Error in UnlockForProcess of child apo; detaching child");
			reset();
		}
	}
}

void ChildApo::process(UINT32 inputCount, APO_CONNECTION_PROPERTY** inputs,
	UINT32 outputCount, APO_CONNECTION_PROPERTY** outputs) noexcept
{
	if (childRT)
		childRT->APOProcess(inputCount, inputs, outputCount, outputs);
}

void ChildApo::reset() noexcept
{
	childAPO.reset();
	childRT.reset();
	childCfg.reset();
}
