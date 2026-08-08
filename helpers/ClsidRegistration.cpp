/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "stdafx.h"
#include "ClsidRegistration.h"

namespace
{
const wchar_t* const clsidRoot = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID\\";
}

namespace ClsidRegistration
{
void registerClsidTree(IRegistry& registry, const std::wstring& clsidString,
	const std::wstring& className, const std::wstring& dllPath)
{
	const std::wstring classKey = clsidRoot + clsidString;

	registry.createKey(classKey);
	registry.writeValue(classKey, L"", className);
	registry.createKey(classKey + L"\\InprocServer32");
	registry.writeValue(classKey + L"\\InprocServer32", L"", dllPath);
	registry.writeValue(classKey + L"\\InprocServer32", L"ThreadingModel", L"Both");
}

void unregisterClsidTree(IRegistry& registry, const std::wstring& clsidString)
{
	const std::wstring classKey = clsidRoot + clsidString;

	registry.deleteKey(classKey + L"\\InprocServer32");
	registry.deleteKey(classKey);
}
}
