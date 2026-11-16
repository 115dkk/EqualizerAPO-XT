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

std::wstring detectionOutput(const std::wstring& channel, const std::wstring& url)
{
	return channel + L"\n" + url + L"\n";
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

std::wstring httpFailure(unsigned long statusCode)
{
	return L"The download server returned HTTP " + std::to_wstring(statusCode) + L".";
}

InstallResult install(const CpuFeatures& features, bool silent, const InstallOperations& operations)
{
	InstallResult result;
	const auto stage = [&](InstallStage next, const std::wstring& detail = std::wstring())
	{
		result.stage = next;
		if (operations.stageChanged)
			operations.stageChanged(next, detail);
	};
	const auto acceptDownload = [&](const DownloadResult& download)
	{
		result.httpStatus = download.statusCode;
		result.error = download.statusCode != 0 && download.statusCode != 200
			? httpFailure(download.statusCode) : download.error;
		return download.statusCode == 200 && result.error.empty();
	};

	stage(InstallStage::Detect);
	const std::wstring channel = channelForCpu(features);
	const std::wstring name = assetName(channel);
	stage(InstallStage::Download, channel);
	std::unique_ptr<HeldSetup> file = operations.createFile(name, result.error);
	if (!file)
		return result;
	const DownloadResult setup = operations.transport(downloadUrl(channel),
		[&](std::string_view bytes, std::wstring& error) { return file->write(bytes, error); },
		operations.progress);
	if (!acceptDownload(setup))
		return result;

	stage(InstallStage::Verify);
	// Checksums asset that CI publishes to every release, one sha256sum-style
	// "<lowercase-hex-sha256>  <name>" line per asset. The grammar header keeps
	// this in step with the upload in .github/workflows/build.yml.
	std::string checksums;
	const DownloadResult sums = operations.transport(
		L"https://github.com" + latestAssetPath(ReleaseAssetNames::checksumsAssetName),
		[&](std::string_view bytes, std::wstring& error)
		{
			// Bound the untrusted response before appending, not after allocating
			// or writing it to a predictable file (audit #467 TD-56).
			if (bytes.size() > 1024 * 1024 - checksums.size())
			{
				error = L"The integrity checksums file could not be read.";
				return false;
			}
			checksums.append(bytes);
			return true;
		}, {});
	if (!acceptDownload(sums))
		return result;
	const std::wstring expected = expectedHashFromChecksums(checksums, name);
	if (expected.empty())
	{
		result.error = L"The release's checksums file does not list the downloaded installer.";
		return result;
	}
	std::wstring actual;
	if (!file->sealAndHash(actual, result.error))
		return result;
	// SHA256SUMS.txt comes from the same release: this checks integrity, not
	// authenticity. This is the ceiling until the installers are signed.
	if (actual != expected)
	{
		result.error = L"The downloaded installer failed its integrity check.";
		return result;
	}

	stage(InstallStage::Launch, actual);
	// Pass the very same live holder to the launcher. No pathname is reopened
	// for verification, and the reader outlives CreateProcess (audit #467 TD-56).
	const LaunchResult launched = operations.launch(*file, silent);
	if (!launched.started)
	{
		result.error = L"The downloaded installer could not be started.";
		return result;
	}
	result.exitCode = silent ? launched.exitCode : 0;
	stage(InstallStage::Complete);
	return result;
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
