/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "UpdateLeftovers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/windows/Win32Resource.h"
#include "services/logging/TaggedLogger.h"

#include <limits>

namespace
{
constexpr logging::TaggedLogger logLine(L"UpdateLeftovers");
constexpr ULONGLONG kFileTimeTicksPerSecond = 10000000;

bool isPathSeparator(wchar_t ch)
{
	return ch == L'\\' || ch == L'/';
}

bool isAsciiAlphaNumeric(wchar_t ch)
{
	return (ch >= L'0' && ch <= L'9')
		|| (ch >= L'A' && ch <= L'Z')
		|| (ch >= L'a' && ch <= L'z');
}

bool isAsciiAlphaNumericSuffix(std::wstring_view value)
{
	if (value.size() != 16)
		return false;
	for (wchar_t ch : value)
	{
		if (!isAsciiAlphaNumeric(ch))
			return false;
	}
	return true;
}

bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
{
	if (left.size() != right.size())
		return false;
	if (left.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
		return false;
	return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
		right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::wstring absolutePath(const std::wstring& path)
{
	if (path.empty())
		return std::wstring();
	const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
	if (required == 0)
		return std::wstring();
	std::wstring result(required, L'\0');
	const DWORD length = GetFullPathNameW(path.c_str(), required, result.data(), nullptr);
	if (length == 0 || length >= required)
		return std::wstring();
	result.resize(length);
	return result;
}

std::wstring trimTrailingSeparators(std::wstring path)
{
	while (path.size() > 1 && isPathSeparator(path.back()))
	{
		if (path.size() == 3 && path[1] == L':')
			break;
		path.pop_back();
	}
	return path;
}

std::wstring joinPath(const std::wstring& parent, std::wstring_view child)
{
	if (parent.empty())
		return std::wstring(child);
	std::wstring result = parent;
	if (!isPathSeparator(result.back()))
		result.push_back(L'\\');
	result.append(child);
	return result;
}

std::wstring leafName(const std::wstring& path)
{
	const size_t separator = path.find_last_of(L"\\/");
	if (separator == std::wstring::npos)
		return path;
	if (separator + 1 == path.size())
		return std::wstring();
	return path.substr(separator + 1);
}

std::wstring parentPath(const std::wstring& path)
{
	const size_t separator = path.find_last_of(L"\\/");
	if (separator == std::wstring::npos)
		return std::wstring();
	if (separator == 2 && path.size() >= 3 && path[1] == L':')
		return path.substr(0, 3);
	return path.substr(0, separator);
}

bool isDirectory(const std::wstring& path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool isFile(const std::wstring& path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

ULONGLONG fileTimeValue(const FILETIME& value)
{
	ULARGE_INTEGER integer{};
	integer.LowPart = value.dwLowDateTime;
	integer.HighPart = value.dwHighDateTime;
	return integer.QuadPart;
}

bool isOldEnough(const FILETIME& creationTime, const FILETIME& now,
	std::chrono::seconds minimumAge)
{
	const auto seconds = minimumAge.count();
	if (seconds <= 0)
		return true;
	const ULONGLONG created = fileTimeValue(creationTime);
	const ULONGLONG current = fileTimeValue(now);
	if (created > current)
		return false;
	const ULONGLONG unsignedSeconds = static_cast<ULONGLONG>(seconds);
	if (unsignedSeconds > (std::numeric_limits<ULONGLONG>::max)() / kFileTimeTicksPerSecond)
		return false;
	return current - created >= unsignedSeconds * kFileTimeTicksPerSecond;
}

template<typename Function>
void forEachEntry(const std::wstring& directory, Function&& function)
{
	WIN32_FIND_DATAW data{};
	winutil::UniqueFindHandle findHandle(FindFirstFileW(joinPath(directory, L"*").c_str(), &data));
	if (!findHandle)
		return;
	do
	{
		const std::wstring_view name(data.cFileName);
		if (name == L"." || name == L"..")
			continue;
		function(data);
	}
	while (FindNextFileW(findHandle.get(), &data) != FALSE);
}

bool hasRollbackShape(const std::wstring& path)
{
	return isFile(joinPath(path, L"Update.exe"))
		|| isDirectory(joinPath(path, L"current"));
}

void findTempEntries(const std::wstring& root, const FILETIME& now,
	std::chrono::seconds minimumAge, std::vector<std::wstring>& result)
{
	const std::wstring tempDirectory = joinPath(joinPath(root, L"packages"), L"VelopackTemp");
	forEachEntry(tempDirectory, [&](const WIN32_FIND_DATAW& data) {
		const DWORD attributes = data.dwFileAttributes;
		if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0
			|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
			|| !UpdateLeftovers::isTempEntryName(data.cFileName)
			|| !isOldEnough(data.ftCreationTime, now, minimumAge))
		{
			return;
		}
		result.push_back(joinPath(tempDirectory, data.cFileName));
	});
}

void findRollbackRoots(const std::wstring& root, const FILETIME& now,
	std::chrono::seconds minimumAge, std::vector<std::wstring>& result)
{
	const std::wstring rootLeaf = leafName(root);
	const std::wstring rootParent = parentPath(root);
	if (rootLeaf.empty() || rootParent.empty())
		return;
	forEachEntry(rootParent, [&](const WIN32_FIND_DATAW& data) {
		const DWORD attributes = data.dwFileAttributes;
		if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0
			|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
			|| !UpdateLeftovers::isRollbackRootName(rootLeaf, data.cFileName)
			|| !isOldEnough(data.ftCreationTime, now, minimumAge))
		{
			return;
		}
		const std::wstring candidate = joinPath(rootParent, data.cFileName);
		if (hasRollbackShape(candidate))
			result.push_back(candidate);
	});
}

void clearReadOnly(const std::wstring& path, DWORD attributes)
{
	if ((attributes & FILE_ATTRIBUTE_READONLY) != 0)
		SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
}

void removeLeaf(const std::wstring& path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return;
	clearReadOnly(path, attributes);
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		RemoveDirectoryW(path.c_str());
	else
		DeleteFileW(path.c_str());
}

struct DirectoryEntry
{
	std::wstring name;
};

void removePath(const std::wstring& path, bool orderRollbackRoot = false)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return;
	if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0
		|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		removeLeaf(path);
		return;
	}

	std::vector<DirectoryEntry> entries;
	forEachEntry(path, [&](const WIN32_FIND_DATAW& data) {
		entries.push_back({data.cFileName});
	});

	auto removeMatching = [&](std::wstring_view selectedName, bool inverse) {
		for (const DirectoryEntry& entry : entries)
		{
			const bool matches = equalsIgnoreCase(entry.name, selectedName);
			if (matches != inverse)
				removePath(joinPath(path, entry.name));
		}
	};

	if (orderRollbackRoot)
	{
		for (const DirectoryEntry& entry : entries)
		{
			if (!equalsIgnoreCase(entry.name, L"Update.exe")
				&& !equalsIgnoreCase(entry.name, L"current"))
			{
				removePath(joinPath(path, entry.name));
			}
		}
		removeMatching(L"Update.exe", false);
		removeMatching(L"current", false);
	}
	else
	{
		for (const DirectoryEntry& entry : entries)
			removePath(joinPath(path, entry.name));
	}
	removeLeaf(path);
}

bool pathIsGone(const std::wstring& path)
{
	if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
		return false;
	const DWORD error = GetLastError();
	return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool isRollbackPath(const std::wstring& root, const std::wstring& candidate)
{
	return equalsIgnoreCase(parentPath(root), parentPath(candidate))
		&& UpdateLeftovers::isRollbackRootName(leafName(root), leafName(candidate));
}
}

bool UpdateLeftovers::isTempEntryName(std::wstring_view name)
{
	return name.size() == 20
		&& name.substr(0, 4) == L"tmp_"
		&& isAsciiAlphaNumericSuffix(name.substr(4));
}

bool UpdateLeftovers::isRollbackRootName(std::wstring_view rootLeafName,
	std::wstring_view name)
{
	if (rootLeafName.empty() || name.size() != rootLeafName.size() + 17)
		return false;
	if (!equalsIgnoreCase(rootLeafName, name.substr(0, rootLeafName.size())))
		return false;
	const wchar_t separator = name[rootLeafName.size()];
	return (separator == L'_' || separator == L'.')
		&& isAsciiAlphaNumericSuffix(name.substr(rootLeafName.size() + 1));
}

std::vector<std::wstring> UpdateLeftovers::find(const std::wstring& installRoot,
	std::chrono::seconds minimumAge)
{
	std::vector<std::wstring> result;
	const std::wstring root = trimTrailingSeparators(absolutePath(installRoot));
	if (root.empty() || !isDirectory(root) || !isFile(joinPath(root, L"Update.exe")))
		return result;

	FILETIME now{};
	GetSystemTimeAsFileTime(&now);
	findTempEntries(root, now, minimumAge, result);
	findRollbackRoots(root, now, minimumAge, result);
	return result;
}

UpdateLeftovers::SweepResult UpdateLeftovers::sweep(const std::wstring& installRoot,
	std::chrono::seconds minimumAge)
{
	SweepResult result;
	try
	{
		const std::wstring root = trimTrailingSeparators(absolutePath(installRoot));
		const std::vector<std::wstring> leftovers = find(root, minimumAge);
		result.found = leftovers.size();
		for (const std::wstring& leftover : leftovers)
		{
			try
			{
				removePath(leftover, isRollbackPath(root, leftover));
				if (pathIsGone(leftover))
					++result.removed;
			}
			catch (...)
			{
			}
		}
	}
	catch (...)
	{
	}
	if (result.found > 0)
		logLine(L"INFO", L"found %zu update leftover directories; removed %zu", result.found, result.removed);
	return result;
}
