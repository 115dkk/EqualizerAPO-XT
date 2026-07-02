#pragma once

#include <sstream>
#include <string>

class ConfigurationFileReader
{
public:
	static std::stringstream readWithRetry(const std::wstring& path);
};
