/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The release asset-name grammar for C++ consumers, mirroring
	.github/scripts/ReleaseAssets.psm1 (audit #250 F067). The channel appears
	twice in the setup name because the Velopack pack id already embeds it and
	vpk appends the channel again. ReleaseAssets.Tests.ps1 asserts this header
	and the PowerShell module produce the same names, so a grammar change that
	lands in only one language fails the Pester gate.

	Header-only std C++ on purpose: the auto-detect installer is a standalone
	32-bit binary that links no project library, and UpdateChecker compiles
	under Qt - both can include this, neither can link Common.
*/

#pragma once

#include <string>

namespace ReleaseAssetNames
{
inline constexpr wchar_t productPrefix[] = L"EqualizerAPO-XT";
inline constexpr wchar_t checksumsAssetName[] = L"SHA256SUMS.txt";

inline std::wstring velopackPackId(const std::wstring& channel)
{
	return std::wstring(productPrefix) + L"-" + channel;
}

inline std::wstring setupAssetName(const std::wstring& channel)
{
	return velopackPackId(channel) + L"-" + channel + L"-Setup.exe";
}

inline std::wstring universalSetupAssetName()
{
	return std::wstring(productPrefix) + L"-Setup.exe";
}
}
