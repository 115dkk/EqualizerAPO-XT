#include "stdafx.h"

#include "ConfigurationFileReader.h"

#include <windows.h>

#include "helpers/LogHelper.h"
#include "helpers/StringHelper.h"

std::stringstream ConfigurationFileReader::readWithRetry(const std::wstring& path)
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION)
			{
				LogFStatic(L"Error while reading configuration file %s: %s", path.c_str(), StringHelper::getSystemErrorString(error).c_str());
				return {};
			}

			Sleep(1);
		}
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
