/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "services/install/VcRuntime.h"
#include "platform/windows/CommandLineQuoting.h"
#include "platform/windows/Win32Resource.h"
#include "platform/windows/WindowsPath.h"
#include "services/logging/TaggedLogger.h"
#include "services/registry/WindowsRegistry.h"
#include <vector>

namespace
{
constexpr logging::TaggedLogger logLine(L"VcRuntime");

std::optional<vcruntime::Version> installedVersion(const std::wstring& architecture)
{
	const std::wstring key = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\" + architecture;
	const auto& registry = systemRegistry();
	try
	{
		if (!registry.keyExists(key) || registry.readDWORDValue(key, L"Installed") != 1)
			return std::nullopt;
		return vcruntime::Version{registry.readDWORDValue(key, L"Major"),
			registry.readDWORDValue(key, L"Minor"), registry.readDWORDValue(key, L"Bld"),
			registry.readDWORDValue(key, L"Rbld")};
	}
	catch (const RegistryError& e)
	{
		logLine(L"WARN", L"Cannot read installed runtime version: %s", e.getMessage().c_str());
		return std::nullopt;
	}
}

std::optional<vcruntime::Version> fileVersion(const std::wstring& path)
{
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
	if (size == 0)
		return std::nullopt;
	std::vector<char> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
		return std::nullopt;
	void* value = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.data(), L"\\", &value, &length) || length < sizeof(VS_FIXEDFILEINFO))
		return std::nullopt;
	const auto* info = static_cast<const VS_FIXEDFILEINFO*>(value);
	if (info->dwSignature != 0xfeef04bd)
		return std::nullopt;
	return vcruntime::Version{HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
		HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
}

vcruntime::ProcessResult startInstaller(const std::wstring& path, const std::wstring& directory)
{
	std::wstring command = winutil::joinCommandLineArguments({path, L"/install", L"/quiet", L"/norestart"});
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		nullptr, directory.c_str(), &startup, &process))
	{
		logLine(L"WARN", L"Redistributable launch failed (error=%lu)", GetLastError());
		return {};
	}
	winutil::UniqueHandle thread(process.hThread);
	winutil::UniqueHandle child(process.hProcess);
	// VC setup can take minutes; consuming the hook's 12 s would prevent audio
	// recovery. Poll only, then let setup finish independently (audit #467 question 4).
	if (WaitForSingleObject(child.get(), 0) != WAIT_OBJECT_0)
		return {true, std::nullopt};
	DWORD code = 0;
	if (!GetExitCodeProcess(child.get(), &code))
		return {true, std::nullopt};
	logLine(L"INFO", L"Redistributable exit code=%lu", code);
	return {true, code};
}
}

namespace vcruntime
{
bool needsInstall(const std::optional<Version>& installed, const std::optional<Version>& shipped)
{
	return shipped.has_value() && (!installed.has_value() || *installed < *shipped);
}

Outcome runIfNeeded(const std::optional<Version>& installed, const std::optional<Version>& shipped,
	const Runner& runner)
{
	if (!needsInstall(installed, shipped))
		return Outcome::NotNeeded;
	const auto result = runner();
	if (!result.started)
		return Outcome::Failed;
	if (!result.exitCode)
		return Outcome::Running;
	const auto code = *result.exitCode;
	return code == 0 || code == 3010 || code == 1638 ? Outcome::Completed : Outcome::Failed;
}

void ensureInstalled(const std::wstring& installDir)
{
	try
	{
#ifdef _M_ARM64
		const std::wstring architecture = L"arm64";
#else
		const std::wstring architecture = L"x64";
#endif
		const auto path = pathutil::joinPath(installDir, L"vc_redist." + architecture + L".exe");
		const auto shipped = fileVersion(path);
		if (!shipped)
		{
			logLine(L"WARN", L"Shipped redistributable missing or without a readable version: %s", path.c_str());
			return;
		}
		const auto result = runIfNeeded(installedVersion(architecture), shipped,
			[&]() { return startInstaller(path, installDir); });
		switch (result)
		{
		case Outcome::NotNeeded:
			logLine(L"INFO", L"Installed VC runtime is at least the shipped version");
			break;
		case Outcome::Completed:
			logLine(L"INFO", L"Redistributable completed successfully; a restart may be required");
			break;
		case Outcome::Running:
			logLine(L"INFO", L"Redistributable started independently: %s; setup logs its final result in TEMP", path.c_str());
			break;
		case Outcome::Failed:
			logLine(L"WARN", L"Redistributable failed; continuing the install hook");
			break;
		}
	}
	catch (...)
	{
		logLine(L"WARN", L"Runtime preparation failed; continuing the install hook");
	}
}
}
