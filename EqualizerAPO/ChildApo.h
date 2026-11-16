/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <functional>
#include <string>
#include "platform/windows/ComPtr.h"
#include <audioenginebaseapo.h>

// Owns the vendor effect independently of the COM shell, so its lifecycle can
// be tested without activating or registering a COM class (audit #467 TD-26).
// Lifecycle calls are serialized by the host and never overlap process().
class ChildApo
{
public:
	using Activator = std::function<HRESULT(REFCLSID, IAudioProcessingObject**)>;
	enum class AttachResult
	{
		NoChild, Attached, BadClsid, ActivationFailed, NoRealtimeInterface,
		NoConfigurationInterface, InitializeFailed
	};

	ChildApo() = default;
	~ChildApo();
	ChildApo(const ChildApo&) = delete;
	ChildApo& operator=(const ChildApo&) = delete;
	ChildApo(ChildApo&&) = delete;
	ChildApo& operator=(ChildApo&&) = delete;

	// Replaces any previous child. Empty text or any failed HRESULT leaves this
	// detached; the shell treats all attach results as successful initialization.
	AttachResult attach(const std::wstring& clsidText, UINT32 cbDataSize, BYTE* pbyData,
		const Activator& activate = coCreate);
	bool attached() const noexcept;
	HRESULT isInputFormatSupported(IAudioMediaType* outputFormat,
		IAudioMediaType* requestedInputFormat, IAudioMediaType** supportedInputFormat);
	HRESULT getLatency(HNSTIME* time);
	bool lockForProcess(UINT32 inputCount, APO_CONNECTION_DESCRIPTOR** inputs,
		UINT32 outputCount, APO_CONNECTION_DESCRIPTOR** outputs);
	void unlockForProcess();
	// Real-time forwarding only: no allocation, synchronization, or logging.
	void process(UINT32 inputCount, APO_CONNECTION_PROPERTY** inputs,
		UINT32 outputCount, APO_CONNECTION_PROPERTY** outputs) noexcept;
	void reset() noexcept;

private:
	static HRESULT coCreate(REFCLSID clsid, IAudioProcessingObject** object);
	winutil::ComPtr<IAudioProcessingObject> childAPO;
	winutil::ComPtr<IAudioProcessingObjectRT> childRT;
	winutil::ComPtr<IAudioProcessingObjectConfiguration> childCfg;
};
