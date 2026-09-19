/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"

#include <cwctype>
#include <string>

#include "ConfigPathPolicy.h"

namespace
{
bool isSeparator(wchar_t character)
{
	return character == L'\\' || character == L'/';
}
}

std::wstring ConfigPathPolicy::remoteRoot(const std::wstring& path)
{
	if (path.size() < 2 || !isSeparator(path[0]) || !isSeparator(path[1]))
		return L"";

	size_t index = 2;
	while (index < path.size() && isSeparator(path[index]))
		++index;

	std::wstring components[2];
	for (size_t component = 0; component < 2 && index < path.size(); ++component)
	{
		const size_t begin = index;
		while (index < path.size() && !isSeparator(path[index]))
			++index;
		components[component] = path.substr(begin, index - begin);
		while (index < path.size() && isSeparator(path[index]))
			++index;
	}

	std::wstring root = L"\\\\" + components[0];
	if (!components[1].empty())
		root += L"\\" + components[1];
	for (wchar_t& character : root)
		character = static_cast<wchar_t>(std::towlower(character));
	return root;
}

bool ConfigPathPolicy::allowsOpen(const std::wstring& path, const std::wstring& configPath, std::wstring& reason)
{
	const std::wstring root = remoteRoot(path);
	if (root.empty())
		return true;
	if (!configPath.empty() && root == remoteRoot(configPath))
		return true;

	reason = L"\"" + path
		+ L"\" is on a network share or a device path; the audio engine only opens files on local drives, so copy the file into the configuration folder";
	return false;
}
