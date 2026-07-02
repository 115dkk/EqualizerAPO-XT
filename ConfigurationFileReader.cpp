#include "stdafx.h"

#include "ConfigurationFileReader.h"

#include <windows.h>

#include "helpers/FileSharingRetry.h"
#include "helpers/LogHelper.h"
#include "helpers/StringHelper.h"

std::stringstream ConfigurationFileReader::readWithRetry(const std::wstring& path)
{
	DWORD error = ERROR_SUCCESS;
	HANDLE hFile = openFileWithSharingRetry(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, error);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), StringHelper::getSystemErrorString(error).c_str());
		return {};
	}

	std::stringstream inputStream;
	char buf[8192];
	unsigned long bytesRead = 0;
	while (ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead != 0)
		inputStream.write(buf, bytesRead);

	CloseHandle(hFile);
	inputStream.seekg(0);
	return inputStream;
}
