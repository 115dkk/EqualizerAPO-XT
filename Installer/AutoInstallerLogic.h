/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The auto-detect installer's decision logic, split out of the wWinMain
	translation unit (audit #250 F056) so a test suite can compile it: which
	channel a CPU maps to, which asset that names, and how the sha256sum
	checksums text is read. The Win32 work (CPUID/XGETBV gathering, WinHTTP,
	BCrypt, process launch) stays in AutoInstaller.cpp; this unit only
	decides. EditorLogicTests compiles AutoInstallerLogic.cpp directly, so
	PRs verify logic the release-only installer build used to leave until
	release day.

	Like the installer itself this must stay free of project libraries: it
	is std C++ plus the grammar header and version.h.
*/

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

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

// "x64-avx2" -> "64-bit x86 with AVX2" and so on, from the same table
// channelForCpu reads. Unknown channels come back verbatim so a future
// channel never renders as an empty line.
std::wstring describeChannel(const std::wstring& channel);

// Per-variant installer asset name; the shared grammar header explains the
// doubled channel.
std::wstring assetName(const std::wstring& channel);

// Always-latest download path. GitHub redirects /releases/latest/download/<asset>
// to the newest release's asset, so this binary never needs rebuilding per release.
std::wstring latestAssetPath(const std::wstring& asset);
std::wstring assetPath(const std::wstring& channel);
std::wstring downloadUrl(const std::wstring& channel);

// --detect-only stdout and --out share this exact two-line, LF-terminated text.
std::wstring detectionOutput(const std::wstring& channel, const std::wstring& url);

// Find fileName in sha256sum-style checksum text and return its digest as
// lowercase hex. Each line is "<64 hex chars>  <name>"; the binary-mode form
// "<hash> *<name>" is accepted too, and the name comparison ignores ASCII
// case. Returns an empty string when no line matches.
std::wstring expectedHashFromChecksums(const std::string& text, const std::wstring& fileName);

std::wstring httpFailure(unsigned long statusCode);

// The transport delivers HTTP 200 bytes to a sink in bounded chunks; it never
// writes a path itself. This keeps the installer streamed and checksums in
// memory (audit #467 TD-56/TD-25).
using ByteSink = std::function<bool(std::string_view, std::wstring&)>;
using DownloadProgress = std::function<void(unsigned long long, unsigned long long)>;
struct DownloadResult
{
	unsigned long statusCode = 0;
	std::wstring error;
};

class HeldSetup
{
public:
	virtual ~HeldSetup() = default;
	virtual bool write(std::string_view bytes, std::wstring& error) = 0;
	// Close the writer, deny writes/deletion, then hash through the held reader.
	// The reader and its directory stay pinned until this object is destroyed.
	virtual bool sealAndHash(std::wstring& hash, std::wstring& error) = 0;
};

struct LaunchResult
{
	bool started = false;
	unsigned long exitCode = 0;
};

enum class InstallStage { Detect, Download, Verify, Launch, Complete };
struct InstallResult
{
	InstallStage stage = InstallStage::Detect;
	unsigned long exitCode = 0;
	unsigned long httpStatus = 0;
	std::wstring error;
};

struct InstallOperations
{
	std::function<DownloadResult(const std::wstring&, const ByteSink&, const DownloadProgress&)> transport;
	std::function<std::unique_ptr<HeldSetup>(const std::wstring&, std::wstring&)> createFile;
	std::function<LaunchResult(HeldSetup&, bool)> launch;
	// Stage starts; detail is the channel for Download and the verified hash
	// for Launch. Optional, for the existing window's state updates only.
	std::function<void(InstallStage, const std::wstring&)> stageChanged;
	DownloadProgress progress;
};

InstallResult install(const CpuFeatures& features, bool silent, const InstallOperations& operations);

bool hasFlag(int argc, wchar_t** argv, const wchar_t* flag);
const wchar_t* flagValue(int argc, wchar_t** argv, const wchar_t* flag);
}
