#pragma once

#include <string>

class ConvolutionFilePath
{
public:
	static std::wstring normalizeParameter(const std::wstring& parameters);
	static std::wstring resolve(const std::wstring& configPath, const std::wstring& parameters);
};
