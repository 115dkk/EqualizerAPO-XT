/*
	This file is part of EqualizerAPO-XT.

	Kept intentionally free of project libraries: the installer is a
	standalone 32-bit binary (see AutoInstaller.cpp) and EditorLogicTests
	compiles this file directly.
*/

#include "AutoInstallerLogic.h"

#include <windows.h>
#include <wchar.h>

#include "../release/ReleaseAssetNames.h"
#include "../version.h"

namespace AutoInstallerLogic
{
namespace
{
bool isHexDigit(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char toLowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
}

std::wstring channelForCpu(const CpuFeatures& features, int* outIndex)
{
	if (features.arm64Native)
	{
		if (outIndex != nullptr)
			*outIndex = kArm64;
		return L"arm64-neon";
	}
	// Most specific / newest first.
	if (features.avx10_1)
	{
		if (outIndex != nullptr)
			*outIndex = kAvx10_1;
		return L"x64-avx10-1";
	}
	if (features.avx512f)
	{
		if (outIndex != nullptr)
			*outIndex = kAvx512;
		return L"x64-avx512";
	}
	if (features.avx2)
	{
		if (outIndex != nullptr)
			*outIndex = kAvx2;
		return L"x64-avx2";
	}
	if (features.avx)
	{
		if (outIndex != nullptr)
			*outIndex = kAvx;
		return L"x64-avx";
	}
	if (outIndex != nullptr)
		*outIndex = kSse2;
	return L"x64-sse2";
}

std::wstring assetName(const std::wstring& channel)
{
	return ReleaseAssetNames::setupAssetName(channel);
}

std::wstring latestAssetPath(const std::wstring& asset)
{
	return std::wstring(L"/") + EAPO_REPO_SLUG_W +
		L"/releases/latest/download/" + asset;
}

std::wstring assetPath(const std::wstring& channel)
{
	return latestAssetPath(assetName(channel));
}

std::wstring downloadUrl(const std::wstring& channel)
{
	return std::wstring(L"https://github.com") + assetPath(channel);
}

std::wstring expectedHashFromChecksums(const std::string& text, const std::wstring& fileName)
{
	std::string narrowName;
	int needed = WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, nullptr, 0,
		nullptr, nullptr);
	if (needed > 1)
	{
		narrowName.resize(needed - 1);
		WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, &narrowName[0], needed - 1,
			nullptr, nullptr);
	}
	if (narrowName.empty())
		return std::wstring();

	// Skip a UTF-8 byte order mark, in case the generator wrote one.
	size_t lineStart = 0;
	if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		lineStart = 3;

	while (lineStart < text.size())
	{
		size_t lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string::npos)
			lineEnd = text.size();
		std::string line = text.substr(lineStart, lineEnd - lineStart);
		lineStart = lineEnd + 1;
		while (!line.empty() &&
			(line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
		{
			line.pop_back();
		}

		// 64 hex digits, separating whitespace, an optional '*' binary-mode
		// marker, then the asset name.
		if (line.size() < 64 + 2)
			continue;
		bool hexOk = true;
		for (size_t i = 0; i < 64; ++i)
		{
			if (!isHexDigit(line[i]))
			{
				hexOk = false;
				break;
			}
		}
		if (!hexOk || (line[64] != ' ' && line[64] != '\t'))
			continue;

		size_t nameStart = 64;
		while (nameStart < line.size() && (line[nameStart] == ' ' || line[nameStart] == '\t'))
			++nameStart;
		if (nameStart < line.size() && line[nameStart] == '*')
			++nameStart;
		if (nameStart >= line.size())
			continue;

		const std::string name = line.substr(nameStart);
		if (name.size() != narrowName.size())
			continue;
		bool nameMatches = true;
		for (size_t i = 0; i < name.size(); ++i)
		{
			if (toLowerAscii(name[i]) != toLowerAscii(narrowName[i]))
			{
				nameMatches = false;
				break;
			}
		}
		if (!nameMatches)
			continue;

		std::wstring hex;
		hex.reserve(64);
		for (size_t i = 0; i < 64; ++i)
			hex += static_cast<wchar_t>(toLowerAscii(line[i]));
		return hex;
	}
	return std::wstring();
}

bool hasFlag(int argc, wchar_t** argv, const wchar_t* flag)
{
	for (int i = 1; i < argc; ++i)
	{
		if (_wcsicmp(argv[i], flag) == 0)
			return true;
	}
	return false;
}

const wchar_t* flagValue(int argc, wchar_t** argv, const wchar_t* flag)
{
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (_wcsicmp(argv[i], flag) == 0)
			return argv[i + 1];
	}
	return nullptr;
}
}
