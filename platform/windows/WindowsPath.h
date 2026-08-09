/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Audit #250 F018: joinPath / fileExists / directoryExists / exeDirectory
	used to be copied verbatim into three modules of this static library
	(ApoRegistration, AudioEngineAccess, services/update/VelopackBootstrap). One header-only
	home; consumers pull what they need into their namespace.
*/

#pragma once

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pathutil
{

inline std::wstring joinPath(const std::wstring& directory,
	const std::wstring& leaf)
{
	if (directory.empty())
		return leaf;
	wchar_t last = directory.back();
	if (last == L'\\' || last == L'/')
		return directory + leaf;
	return directory + L"\\" + leaf;
}

inline bool fileExists(const std::wstring& path)
{
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool directoryExists(const std::wstring& path)
{
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Creates every missing directory on the path. True when the directory
// exists afterwards, however much of it already existed.
inline bool createDirectoryRecursive(const std::wstring& path)
{
	if (path.empty() || directoryExists(path))
		return true;
	size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos && slash > 0)
	{
		if (!createDirectoryRecursive(path.substr(0, slash)))
			return false;
	}
	return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

inline bool pathExists(const std::wstring& path)
{
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The directory of the current executable; empty on failure.
inline std::wstring exeDirectory()
{
	wchar_t buffer[MAX_PATH];
	DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::wstring();
	std::wstring path(buffer, length);
	size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return std::wstring();
	return path.substr(0, slash);
}

}
