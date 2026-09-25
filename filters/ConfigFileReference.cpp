/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include <filesystem>
#include "text/WideString.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Shlwapi.h>

#include "ConfigPathPolicy.h"
#include "ConfigFileReference.h"

using std::wstring;
namespace filesystem = std::filesystem;

namespace
{
wstring unquote(const wstring& value)
{
	if (value.length() >= 2 && value.front() == L'"' && value.back() == L'"')
		return value.substr(1, value.length() - 2);
	return value;
}

wstring expandEnvironmentStrings(const wstring& value)
{
	DWORD requiredLength = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
	if (requiredLength == 0)
		return value;

	wstring expanded(requiredLength, L'\0');
	DWORD writtenLength = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), requiredLength);
	if (writtenLength == 0 || writtenLength > requiredLength)
		return value;

	expanded.resize(writtenLength - 1);
	return expanded;
}
}

wstring ConfigFileReference::normalize(const wstring& written)
{
	return expandEnvironmentStrings(unquote(text::trim(written)));
}

wstring ConfigFileReference::resolve(const wstring& configPath, const wstring& written)
{
	wstring value = normalize(written);
	if (value.empty())
		return L"";

	filesystem::path path(value);
	if (path.is_absolute())
		return path.lexically_normal().wstring();

	filesystem::path basePath(configPath);
	basePath.remove_filename();
	return (basePath / path).lexically_normal().wstring();
}

wstring ConfigFileReference::resolveLibrary(const wstring& pluginFolder, const wstring& reference)
{
	if (reference.empty())
		return L"";
	if (!PathIsRelativeW(reference.c_str()))
		return reference;

	wstring folder = pluginFolder;
	while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/'))
		folder.pop_back();
	return folder + L"\\" + reference;
}

ConfigFileReference::Target ConfigFileReference::target(const wstring& configPath, const wstring& written)
{
	Target result;
	result.path = resolve(configPath, written);
	if (!result.path.empty() && !ConfigPathPolicy::allowsOpen(result.path, configPath, result.refusal))
		result.path.clear();
	return result;
}
