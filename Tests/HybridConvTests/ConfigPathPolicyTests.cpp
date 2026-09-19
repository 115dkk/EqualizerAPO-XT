/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Tests for the policy that keeps configuration-referenced file opens on
	local drives while preserving same-share references for network configs.
*/

#include <string>

#include "filters/ConfigPathPolicy.h"
#include "Tests/TestHarness.h"

using std::wstring;

namespace
{
test::Harness harness("ConfigPathPolicyTests");

void expectRoot(const wstring& path, const wstring& expected, const char* message)
{
	harness.expectTrue(ConfigPathPolicy::remoteRoot(path) == expected, message);
}

void testRemoteRoot()
{
	expectRoot(L"\\\\srv\\share\\x.txt", L"\\\\srv\\share", "UNC root keeps the server and share");
	expectRoot(L"//SRV/Share/x", L"\\\\srv\\share", "forward slashes are normalized and case-folded");
	expectRoot(L"\\\\?\\C:\\x", L"\\\\?\\c:", "verbatim drive path has a remote-style root");
	expectRoot(L"\\\\?\\UNC\\srv\\share\\x", L"\\\\?\\unc", "verbatim UNC path uses its first two components");
	expectRoot(L"\\\\.\\pipe\\name", L"\\\\.\\pipe", "device pipe path has a device root");
	expectRoot(L"\\\\srv", L"\\\\srv", "single-component UNC path keeps its component");
	expectRoot(L"C:\\x", L"", "drive-letter absolute path is local");
	expectRoot(L"c:\\x", L"", "lower-case drive-letter path is local");
	expectRoot(L"x\\y", L"", "relative path is local");
	expectRoot(L"\\x", L"", "root-relative path is local");
	expectRoot(L"C:x", L"", "drive-relative path is local");
	expectRoot(L"", L"", "empty path has no remote root");
}

void expectAllowed(const wstring& path, const wstring& configPath, const char* message)
{
	wstring reason;
	harness.expectTrue(ConfigPathPolicy::allowsOpen(path, configPath, reason), message);
}

void expectRefused(const wstring& path, const wstring& configPath, const char* message)
{
	wstring reason;
	harness.expectFalse(ConfigPathPolicy::allowsOpen(path, configPath, reason), message);
	harness.expectFalse(reason.empty(), "a refused path carries a reason");
	harness.expectTrue(reason.find(path) != wstring::npos, "the refusal reason contains the path");
	harness.expectTrue(reason.find(L"network share") != wstring::npos, "the refusal reason names a network share");
}

void testLocalConfigPolicy()
{
	const wstring configPath = L"C:\\cfg\\config.txt";
	expectAllowed(L"C:\\cfg\\a.txt", configPath, "same-drive absolute path is allowed");
	expectAllowed(L"D:\\ir\\a.wav", configPath, "other-drive absolute path is allowed");
	expectAllowed(L"sub\\a.txt", configPath, "relative path is allowed");
	expectAllowed(L"\\a.txt", configPath, "root-relative path is allowed");
	expectAllowed(L"C:a.txt", configPath, "drive-relative path is allowed");
	expectRefused(L"\\\\srv\\share\\a.txt", configPath, "UNC path is refused for a local config");
	expectRefused(L"//srv/share/a.txt", configPath, "forward-slash UNC path is refused for a local config");
	expectRefused(L"\\\\.\\pipe\\x", configPath, "device path is refused for a local config");
	expectRefused(L"\\\\?\\C:\\a.txt", configPath, "verbatim drive path is refused for a local config");
	expectRefused(L"\\\\?\\UNC\\srv\\share\\a.txt", configPath, "verbatim UNC path is refused for a local config");
}

void testSameShareExemption()
{
	const wstring configPath = L"\\\\srv\\share\\config.txt";
	expectAllowed(L"\\\\srv\\share\\sub\\a.txt", configPath, "same-share subpath is allowed");
	expectAllowed(L"\\\\SRV\\SHARE\\a.txt", configPath, "same-share comparison ignores case");
	expectAllowed(L"//srv/share/a.txt", configPath, "same-share comparison accepts forward slashes");
	expectRefused(L"\\\\srv\\other\\a.txt", configPath, "different share is refused");
	expectRefused(L"\\\\other\\share\\a.txt", configPath, "different server is refused");
}

void testEmptyConfigPath()
{
	expectAllowed(L"C:\\a.txt", L"", "local path is allowed without a config path");
	expectRefused(L"\\\\srv\\share\\a.txt", L"", "remote path is refused without a config path");
}
}

void runConfigPathPolicyTests()
{
	testRemoteRoot();
	testLocalConfigPolicy();
	testSameShareExemption();
	testEmptyConfigPath();

	harness.report();
}
