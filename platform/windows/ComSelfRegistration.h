/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace winutil
{
	// Calls DllRegisterServer (or DllUnregisterServer) inside the given COM
	// server DLL, in-process: LOAD_WITH_ALTERED_SEARCH_PATH resolves the DLL's
	// own dependencies (FFTW, libsndfile, ...) relative to its directory, the
	// way the audio engine and regsvr32 load it.
	//
	// One implementation for the two callers that used to carry their own
	// copies (audit #275 TD-04): services/install/ApoRegistration and the
	// recovery branch in devices/DeviceAPOInfo - the latter dropped the
	// HRESULT and logged nothing, so the path users actually hit was the
	// silent one. Every failure (load, missing export, failed entry point) is
	// logged under `logTag`, and the entry point's HRESULT is returned so the
	// caller can act on it.
	HRESULT selfRegisterComServer(const wchar_t* logTag, const std::wstring& dllPath, bool unregister);
}
