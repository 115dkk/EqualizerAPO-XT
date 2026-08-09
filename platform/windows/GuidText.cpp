/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  EqualizerAPO-XT contributors
*/

#include "stdafx.h"

#include "platform/windows/GuidText.h"
#include "platform/windows/Win32Resource.h"
#include "services/registry/RegistryError.h"

std::wstring winutil::guidToString(REFGUID guid)
{
	UniqueCoTaskMemPtr<wchar_t> text;
	if (FAILED(StringFromCLSID(guid, text.put())))
		throw RegistryError(L"Could not convert GUID to string");
	return text.get();
}
