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
}

std::stringstream ConfigurationFileReader::readWithRetry(const std::wstring& path)
{
	DWORD error = ERROR_SUCCESS;
	winutil::UniqueHandle file = openFileWithSharingRetry(
		path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error);
	if (!file)
	{
		LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), win32::errorMessage(error).c_str());
		return makeFailedStream();
	}

	std::stringstream inputStream;
	char buf[8192];
	for (;;)
	{
		DWORD bytesRead = 0;
		if (!ReadFile(file.get(), buf, sizeof(buf), &bytesRead, nullptr))
		{
			error = GetLastError();
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
