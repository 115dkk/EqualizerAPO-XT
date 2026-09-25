/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk
*/

#include "stdafx.h"

#include "services/logging/TaggedLogger.h"
#include "platform/windows/Win32Resource.h"
#include "ComSelfRegistration.h"

namespace winutil
{
HRESULT selfRegisterComServer(const wchar_t* logTag, const std::wstring& dllPath, bool unregister)
{
	const logging::TaggedLogger logLine(logTag);
	winutil::UniqueModule module(LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
	if (!module)
	{
		const DWORD lastError = GetLastError();
		logLine(L"ERR", L"LoadLibrary failed for %s (gle=%lu)", dllPath.c_str(), lastError);
		return HRESULT_FROM_WIN32(lastError);
	}

	using DllServerProc = HRESULT (__stdcall*)();
	const char* entryName = unregister ? "DllUnregisterServer" : "DllRegisterServer";
	DllServerProc proc = reinterpret_cast<DllServerProc>(GetProcAddress(module.get(), entryName));
	if (proc == nullptr)
	{
		const DWORD lastError = GetLastError();
		logLine(L"ERR", L"%S not found in %s (gle=%lu)", entryName, dllPath.c_str(), lastError);
		return HRESULT_FROM_WIN32(lastError);
	}

	const HRESULT hr = proc();
	if (FAILED(hr))
		logLine(L"ERR", L"%S failed for %s (hr=0x%08lX)", entryName, dllPath.c_str(), static_cast<unsigned long>(hr));
	return hr;
}
}
