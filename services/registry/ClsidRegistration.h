/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The COM class-registration tree, extracted from the APO DLL's
	DllRegisterServer (audit #250 A3/F002): the CLSID writes were the last
	registry mutations in the machine-changing path that no test could
	reach, because they lived inside a fixed-signature STDAPI export.
	DllRegisterServer keeps its structure (RegisterAPO ordering, the
	catch-and-unregister rollback); the key/value writes go through the
	port here so a fake registry can pin the exact tree - the spelling
	HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID (unified by F022), the
	InprocServer32 default and the Both threading model.

	Deliberately GUID-agnostic (takes the formatted CLSID string) so this
	unit compiles without the APO's ATL headers.
*/

#pragma once

#include <string>

#include "services/registry/IRegistry.h"

namespace ClsidRegistration
{
// Writes HKLM\SOFTWARE\Classes\CLSID\<clsid> with the class display name
// and its InprocServer32 (path + ThreadingModel Both). Registry exceptions
// propagate to the caller, whose rollback puts the APO GUIDs back.
void registerClsidTree(IRegistry& registry, const std::wstring& clsidString,
	const std::wstring& className, const std::wstring& dllPath);

// Deletes the InprocServer32 subkey, then the class key.
void unregisterClsidTree(IRegistry& registry, const std::wstring& clsidString);

// The same tree under an explicit CLSID root (no trailing backslash), for
// the WOW6432Node view a 32-bit host reads: the registry port always opens
// the 64-bit view, where that view is an ordinary key.
void registerClsidTreeAt(IRegistry& registry, const std::wstring& clsidRootPath, const std::wstring& clsidString,
	const std::wstring& className, const std::wstring& dllPath);
}
