#include "stdafx.h"

#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "text/StringHelper.h"
#include "ConvolutionFilePath.h"

using std::wstring;
namespace filesystem = std::filesystem;

namespace
{
wstring unquote(const wstring& value)
{
	if (value.length() >= 2 && value.front() == L'"' && value.back() == L'"')
		return value.substr(1, value.length() - 2);
	return value;
}

wstring expandEnvironmentStrings(const wstring& value)
{
	DWORD requiredLength = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
	if (requiredLength == 0)
		return value;

	wstring expanded(requiredLength, L'\0');
	DWORD writtenLength = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), requiredLength);
	if (writtenLength == 0 || writtenLength > requiredLength)
		return value;

	expanded.resize(writtenLength - 1);
	return expanded;
}
}

wstring ConvolutionFilePath::normalizeParameter(const wstring& parameters)
{
	return expandEnvironmentStrings(unquote(StringHelper::trim(parameters)));
}

wstring ConvolutionFilePath::resolve(const wstring& configPath, const wstring& parameters)
{
	wstring value = normalizeParameter(parameters);
	if (value.empty())
		return L"";

	filesystem::path path(value);
	if (path.is_absolute())
		return path.lexically_normal().wstring();

	filesystem::path basePath(configPath);
	basePath.remove_filename();
	return (basePath / path).lexically_normal().wstring();
}
