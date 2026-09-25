/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include "platform/windows/Win32Resource.h"

// Internal vocabulary shared by the caller-profile check and the config grant.
namespace configaccess
{
inline bool samePath(const std::wstring& left, const std::wstring& right)
{
	return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

inline std::wstring fullLocalPath(const std::wstring& path)
{
	// Reject namespaces, UNC and drive-relative paths before normalization.
	if (path.size() < 3 || !((path[0] >= L'A' && path[0] <= L'Z')
		|| (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':'
		|| (path[2] != L'\\' && path[2] != L'/'))
		return {};
	for (size_t i = 2; i < path.size(); ++i)
	{
		if (path[i] < L' ' || std::wstring(L"<>:\"|?*").find(path[i]) != std::wstring::npos)
			return {};
	}
	const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
	if (needed == 0)
		return {};
	std::wstring full(needed, L'\0');
	const DWORD written = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
	if (written == 0 || written >= needed)
		return {};
	full.resize(written);
	while (full.size() > 3 && full.back() == L'\\')
		full.pop_back();
	return full;
}

inline std::vector<std::wstring> directoryChain(const std::wstring& full)
{
	std::vector<std::wstring> chain{full.substr(0, 3)};
	size_t end = 3;
	while (end < full.size())
	{
		end = full.find(L'\\', end);
		if (end == std::wstring::npos)
			end = full.size();
		chain.push_back(full.substr(0, end));
		++end;
	}
	return chain;
}

inline winutil::UniqueHandle openDirectory(const std::wstring& path, DWORD access, DWORD sharing,
	std::wstring& reason)
{
	winutil::UniqueHandle handle(CreateFileW(path.c_str(), access, sharing, nullptr, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
	if (!handle)
	{
		reason = L"cannot open directory " + path + L" (error " + std::to_wstring(GetLastError()) + L")";
		return {};
	}
	BY_HANDLE_FILE_INFORMATION info = {};
	if (!GetFileInformationByHandle(handle.get(), &info))
	{
		reason = L"cannot inspect directory " + path;
		return {};
	}
	if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
		|| (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{
		reason = L"not a plain directory: " + path;
		return {};
	}
	return handle;
}
}
