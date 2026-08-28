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

#include <objbase.h>
#include <objidl.h>
#include <shlobj.h>

#include "platform/windows/ComPtr.h"
#include "ShellLink.h"

namespace winutil
{
HRESULT writeShellLink(const std::wstring& target, const std::wstring& workingDir,
	const std::wstring& arguments, const std::wstring& description,
	const std::wstring& iconPath, int iconIndex, const std::wstring& linkPath)
{
	winutil::ComPtr<IShellLinkW> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
		IID_IShellLinkW, reinterpret_cast<void**>(shellLink.put()));
	if (FAILED(hr) || !shellLink)
		return hr;

	shellLink->SetPath(target.c_str());
	if (!workingDir.empty())
		shellLink->SetWorkingDirectory(workingDir.c_str());
	if (!arguments.empty())
		shellLink->SetArguments(arguments.c_str());
	if (!description.empty())
		shellLink->SetDescription(description.c_str());
	if (!iconPath.empty())
		shellLink->SetIconLocation(iconPath.c_str(), iconIndex);

	winutil::ComPtr<IPersistFile> persistFile;
	hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persistFile.put()));
	if (SUCCEEDED(hr) && persistFile)
	{
		hr = persistFile->Save(linkPath.c_str(), TRUE);
	}
	return hr;
}
}
