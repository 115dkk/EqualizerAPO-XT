/*
	This file is part of EqualizerAPO-XT.

	The auto-detect installer's decision logic, split out of the wWinMain
	translation unit (audit #250 F056) so a test suite can compile it: which
	channel a CPU maps to, which asset that names, and how the sha256sum
	checksums text is read. The Win32 work (CPUID/XGETBV gathering, WinHTTP,
	BCrypt, process launch) stays in AutoInstaller.cpp; this unit only
	decides. EditorLogicTests compiles AutoInstallerLogic.cpp directly, the
	same pattern it uses for UpdateChecker's decision core, so PRs verify
	logic the release-only installer build used to leave until release day.

	Like the installer itself this must stay free of project libraries: it
	is std C++ plus the grammar header and version.h.
*/

#pragma once

#include <string>

namespace AutoInstallerLogic
{
// Channel index used as the process exit code for --detect-only, so a script
// can read the detected variant without parsing stdout.
enum ChannelIndex
{
	kSse2 = 0,
	kAvx = 1,
	kAvx2 = 2,
	kAvx512 = 3,
	kAvx10_1 = 4,
	kArm64 = 5
};

// The CPU/OS facts the channel choice depends on. The installer gathers them
// with CPUID/XGETBV (each flag already includes the OS-enabled-state gate, so
// a build the OS cannot context-switch is never picked); tests supply
// fixtures.
struct CpuFeatures
{
	bool arm64Native = false;
	bool avx = false;     // CPU AVX and the OS enabled YMM state
	bool avx2 = false;    // CPUID leaf 7 AVX2, gated on avx
	bool avx512f = false; // AVX-512F, gated on OS ZMM state
	bool avx10_1 = false; // AVX10 version >= 1 with 512-bit vectors, gated on OS ZMM state
};

// Most specific / newest first: arm64, avx10-1, avx512, avx2, avx, sse2.
std::wstring channelForCpu(const CpuFeatures& features, int* outIndex = nullptr);

// Per-variant installer asset name; the shared grammar header explains the
// doubled channel.
std::wstring assetName(const std::wstring& channel);

// Always-latest download path. GitHub redirects /releases/latest/download/<asset>
// to the newest release's asset, so this binary never needs rebuilding per release.
std::wstring latestAssetPath(const std::wstring& asset);
std::wstring assetPath(const std::wstring& channel);
std::wstring downloadUrl(const std::wstring& channel);

// Find fileName in sha256sum-style checksum text and return its digest as
// lowercase hex. Each line is "<64 hex chars>  <name>"; the binary-mode form
// "<hash> *<name>" is accepted too, and the name comparison ignores ASCII
// case. Returns an empty string when no line matches.
std::wstring expectedHashFromChecksums(const std::string& text, const std::wstring& fileName);

bool hasFlag(int argc, wchar_t** argv, const wchar_t* flag);
const wchar_t* flagValue(int argc, wchar_t** argv, const wchar_t* flag);
}
