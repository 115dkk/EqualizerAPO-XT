/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "platform/windows/Win32Error.h"

#include "ConfigurationFileReader.h"

#include <cstdint>
#include <windows.h>

#include "platform/windows/FileSharingRetry.h"
#include "services/logging/Logging.h"

namespace
{
constexpr uint64_t kMaximumConfigurationBytes = 16ull * 1024 * 1024;

std::wstring configurationSizeProblem(const std::wstring& path, uint64_t bytes)
{
	return L"The configuration file \"" + path + L"\" is "
		+ std::to_wstring(bytes) + L" bytes; the limit is "
		+ std::to_wstring(kMaximumConfigurationBytes) + L" bytes";
}

bool configurationFileSize(HANDLE file, uint64_t& bytes)
{
	LARGE_INTEGER size = {};
	if (file == nullptr || !GetFileSizeEx(file, &size) || size.QuadPart < 0)
		return false;
	bytes = static_cast<uint64_t>(size.QuadPart);
	return true;
}

std::stringstream makeFailedStream()
{
	std::stringstream stream;
	stream.setstate(std::ios::badbit);
	return stream;
}

bool readBytesHandle(HANDLE file, std::string& bytes)
{
	uint64_t fileBytes = 0;
	if (!configurationFileSize(file, fileBytes))
		return false;
	if (fileBytes > kMaximumConfigurationBytes)
	{
		SetLastError(ERROR_FILE_TOO_LARGE);
		return false;
	}

	LARGE_INTEGER zero = {};
	if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
		return false;

	bytes.clear();
	char buffer[8192];
	for (;;)
	{
		DWORD bytesRead = 0;
		if (!ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr))
			return false;
		if (bytesRead == 0)
			return true;
		bytes.append(buffer, bytesRead);
	}
}

std::stringstream readHandle(HANDLE file, const std::wstring& path)
{
	uint64_t fileBytes = 0;
	if (configurationFileSize(file, fileBytes) && fileBytes > kMaximumConfigurationBytes)
	{
		LogFStatic(L"%s", configurationSizeProblem(path, fileBytes).c_str());
		return makeFailedStream();
	}

	std::string bytes;
	if (!readBytesHandle(file, bytes))
	{
		const DWORD error = GetLastError();
		LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), win32::errorMessage(error).c_str());
		return makeFailedStream();
	}

	std::stringstream inputStream;
	inputStream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	inputStream.seekg(0);
	return inputStream;
}

}

std::stringstream ConfigurationFileReader::readWithRetry(
	const std::wstring& path, HANDLE cancel, DWORD deadlineMilliseconds)
{
	return readWithRetry(path, cancel, deadlineMilliseconds, systemFileSharingRetryPolicy());
}

std::stringstream ConfigurationFileReader::readWithRetry(const std::wstring& path, HANDLE cancel,
	DWORD deadlineMilliseconds, const FileSharingRetryPolicy& retryPolicy)
{
	DWORD error = ERROR_SUCCESS;
	winutil::UniqueHandle file = openFileWithSharingRetry(
		path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error,
		cancel, deadlineMilliseconds, retryPolicy);
	if (!file)
	{
		LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), win32::errorMessage(error).c_str());
		return makeFailedStream();
	}

	return readHandle(file.get(), path);
}

bool ConfigurationFileReader::readBytes(const JudgedPath& path, std::string& bytes)
{
	return readBytesHandle(path.leaf(), bytes);
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
		if (target)
		{
			uint64_t fileBytes = 0;
			if (configurationFileSize(target.path.leaf(), fileBytes)
				&& fileBytes > kMaximumConfigurationBytes)
			{
				target.refusal = configurationSizeProblem(target.path.path(), fileBytes);
				return target;
			}
		}
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
