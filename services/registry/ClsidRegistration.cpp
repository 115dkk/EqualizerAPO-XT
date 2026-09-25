/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "services/registry/ClsidRegistration.h"
#include "devices/DeviceAPOInfoKeys.h"

namespace ClsidRegistration
{
void registerClsidTreeAt(IRegistry& registry, const std::wstring& clsidRootPath, const std::wstring& clsidString,
	const std::wstring& className, const std::wstring& dllPath)
{
	const std::wstring classKey = clsidRootPath + L"\\" + clsidString;

	registry.createKey(classKey);
	registry.writeValue(classKey, L"", className);
	registry.createKey(classKey + L"\\InprocServer32");
	registry.writeValue(classKey + L"\\InprocServer32", L"", dllPath);
	registry.writeValue(classKey + L"\\InprocServer32", L"ThreadingModel", L"Both");
}

void registerClsidTree(IRegistry& registry, const std::wstring& clsidString,
	const std::wstring& className, const std::wstring& dllPath)
{
	registerClsidTreeAt(registry, clsidKeyPath, clsidString, className, dllPath);
}

void unregisterClsidTree(IRegistry& registry, const std::wstring& clsidString)
{
	const std::wstring classKey = clsidKeyPath L"\\" + clsidString;

	registry.deleteKey(classKey + L"\\InprocServer32");
	registry.deleteKey(classKey);
}
}
