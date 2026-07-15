#include "stdafx.h"

#include "ConfigurationFileReader.h"

#include <windows.h>

#include "helpers/FileSharingRetry.h"
#include "helpers/LogHelper.h"
#include "helpers/StringHelper.h"

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
	HANDLE hFile = openFileWithSharingRetry(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), StringHelper::getSystemErrorString(error).c_str());
		return makeFailedStream();
	}

	std::stringstream inputStream;
	char buf[8192];
	for (;;)
	{
		DWORD bytesRead = 0;
		if (!ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr))
		{
			error = GetLastError();
			CloseHandle(hFile);
			LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), StringHelper::getSystemErrorString(error).c_str());
			return makeFailedStream();
		}
		if (bytesRead == 0)
			break;
		inputStream.write(buf, bytesRead);
	}

	CloseHandle(hFile);
	inputStream.seekg(0);
	return inputStream;
}
