/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

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

struct ChannelEntry
{
	ChannelIndex index;
	const wchar_t* id;
	const wchar_t* description;
};

// The only place the installer spells its channels.
// .github/scripts/Test-VariantSync.ps1 holds the ids to simd-variants.psd1.
const ChannelEntry kChannels[] = {
	{ kSse2, L"x64-sse2", L"64-bit x86 with SSE2" },
	{ kAvx, L"x64-avx", L"64-bit x86 with AVX" },
	{ kAvx2, L"x64-avx2", L"64-bit x86 with AVX2" },
	{ kAvx512, L"x64-avx512", L"64-bit x86 with AVX-512" },
	{ kAvx10_1, L"x64-avx10-1", L"64-bit x86 with AVX10.1" },
	{ kArm64, L"arm64-neon", L"ARM64 with NEON" },
};

const ChannelEntry& entryFor(ChannelIndex index)
{
	for (const ChannelEntry& entry : kChannels)
	{
		if (entry.index == index)
			return entry;
	}
	return kChannels[0];
}

ChannelIndex indexForCpu(const CpuFeatures& features)
{
	if (features.arm64Native)
		return kArm64;
	// Most specific / newest first.
	if (features.avx10_1)
		return kAvx10_1;
	if (features.avx512f)
		return kAvx512;
	if (features.avx2)
		return kAvx2;
	if (features.avx)
		return kAvx;
	return kSse2;
}
}

std::wstring channelForCpu(const CpuFeatures& features, int* outIndex)
{
	const ChannelIndex index = indexForCpu(features);
	if (outIndex != nullptr)
		*outIndex = index;
	return entryFor(index).id;
}

std::wstring describeChannel(const std::wstring& channel)
{
	for (const ChannelEntry& entry : kChannels)
	{
		if (channel == entry.id)
			return entry.description;
	}
	return channel;
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
	if (text.starts_with("\xEF\xBB\xBF"))
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
