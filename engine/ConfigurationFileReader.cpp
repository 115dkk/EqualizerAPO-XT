/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "platform/windows/Win32Error.h"

#include "ConfigurationFileReader.h"

#include <windows.h>

#include "platform/windows/FileSharingRetry.h"
#include "services/logging/Logging.h"

namespace
{
std::stringstream makeFailedStream()
{
	std::stringstream stream;
	stream.setstate(std::ios::badbit);
	return stream;
}

std::stringstream readHandle(HANDLE file, const std::wstring& path)
{
	LARGE_INTEGER zero = {};
	if (file == nullptr || !SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
		return makeFailedStream();
	std::stringstream inputStream;
	char buf[8192];
	for (;;)
	{
		DWORD bytesRead = 0;
		if (!ReadFile(file, buf, sizeof(buf), &bytesRead, nullptr))
		{
			const DWORD error = GetLastError();
			LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), win32::errorMessage(error).c_str());
			return makeFailedStream();
		}
		if (bytesRead == 0)
			break;
		inputStream.write(buf, bytesRead);
	}

	inputStream.seekg(0);
	return inputStream;
}

}

std::stringstream ConfigurationFileReader::readWithRetry(
	const std::wstring& path, HANDLE cancel, DWORD deadlineMilliseconds)
{
	DWORD error = ERROR_SUCCESS;
	winutil::UniqueHandle file = openFileWithSharingRetry(
		path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error, cancel, deadlineMilliseconds);
	if (!file)
	{
		LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), win32::errorMessage(error).c_str());
		return makeFailedStream();
	}

	return readHandle(file.get(), path);
}

std::stringstream ConfigurationFileReader::read(const JudgedPath& path)
{
	return readHandle(path.leaf(), path.path());
}

ConfigFileReference::Target ConfigurationFileReader::judgeWithRetry(const std::wstring& configPath,
	const std::wstring& written, HANDLE cancel, DWORD deadlineMilliseconds)
{
	const ULONGLONG start = GetTickCount64();
	DWORD backoff = 1;
	for (;;)
	{
		auto target = ConfigFileReference::target(configPath, written);
		const ULONGLONG elapsed = GetTickCount64() - start;
		if (target.error != ERROR_SHARING_VIOLATION || elapsed >= deadlineMilliseconds)
			return target;
		const DWORD wait = static_cast<DWORD>((std::min)(ULONGLONG(backoff), deadlineMilliseconds - elapsed));
		if (cancel != nullptr)
		{
			if (WaitForSingleObject(cancel, wait) == WAIT_OBJECT_0)
				return target;
		}
		else
			Sleep(wait);
		backoff = (std::min)(backoff * 2, 20UL);
	}
}
