#pragma once

#include <istream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "text/StringHelper.h"

class ConfigurationFileReader
{
public:
	static std::stringstream readWithRetry(const std::wstring& path);

	static std::vector<std::wstring> decodeLines(std::istream& input)
	{
		std::vector<std::wstring> lines;
		while (input.good())
		{
			std::string encodedLine;
			std::getline(input, encodedLine);
			if (!encodedLine.empty() && encodedLine.back() == '\r')
				encodedLine.pop_back();

			std::wstring line = StringHelper::toWString(encodedLine, CP_UTF8);
			if (line.find(L'\uFFFD') != std::wstring::npos)
				line = StringHelper::toWString(encodedLine, CP_ACP);
			lines.push_back(std::move(line));
		}
		return lines;
	}

	static std::vector<std::wstring> decodeLines(const std::string& bytes)
	{
		std::stringstream input;
		input.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		input.seekg(0);
		return decodeLines(input);
	}
};
