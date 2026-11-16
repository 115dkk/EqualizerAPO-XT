/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The auto-detect installer's decision logic (audit #250 F056). The
	installer binary itself is built release-only, so until this split its
	channel mapping, asset grammar and checksum parsing were verified for
	the first time on release day. These tests compile the logic unit
	directly.
*/

#include <string>

#include "Installer/AutoInstallerLogic.h"

#include "EditorLogicTestSupport.h"

using namespace AutoInstallerLogic;

namespace
{
QString wide(const std::wstring& text)
{
	return QString::fromStdWString(text);
}

struct InstallFixture
{
	struct File final : HeldSetup
	{
		explicit File(InstallFixture& fixture) : fixture(fixture) { fixture.alive = true; }
		~File() override { fixture.alive = false; ++fixture.releases; }
		bool write(std::string_view bytes, std::wstring& error) override
		{
			++fixture.writes;
			fixture.written.append(bytes);
			if (fixture.writeFails)
				error = L"disk write failed";
			return !fixture.writeFails;
		}
		bool sealAndHash(std::wstring& hash, std::wstring& error) override
		{
			fixture.sealed = true;
			if (fixture.hashFails)
			{
				error = L"hash failed";
				return false;
			}
			hash = std::wstring(64, fixture.written == "installer bytes" ? L'a' : L'b');
			return true;
		}
		InstallFixture& fixture;
	};

	InstallOperations operations()
	{
		InstallOperations ops;
		ops.createFile = [&](const std::wstring& name, std::wstring& error) -> std::unique_ptr<HeldSetup>
		{
			createdName = name;
			if (createFails)
			{
				error = L"create failed";
				return nullptr;
			}
			auto file = std::make_unique<File>(*this);
			created = file.get();
			return file;
		};
		ops.transport = [&](const std::wstring& url, const ByteSink& sink, const DownloadProgress& progress)
		{
			urls.push_back(wide(url));
			const bool sums = url.ends_with(L"/SHA256SUMS.txt");
			DownloadResult result;
			result.statusCode = sums ? sumsStatus : setupStatus;
			if (result.statusCode == 200)
			{
				const std::string bytes = sums ? checksums : body;
				if (!sink(bytes, result.error))
					return result;
				if (progress)
					progress(bytes.size(), bytes.size());
			}
			return result;
		};
		ops.launch = [&](const HeldSetup& file, bool silent)
		{
			++launches;
			expectTrue(alive && sealed, "launcher receives a live sealed file");
			expectTrue(&file == created, "launcher receives exactly the holder that was hashed");
			expectTrue(written == "installer bytes", "launcher sees only verified bytes");
			wasSilent = silent;
			return LaunchResult{ !launchFails, 37 };
		};
		return ops;
	}

	std::string body = "installer bytes";
	std::string checksums = std::string(64, 'a') + "  EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe\n";
	unsigned long setupStatus = 200;
	unsigned long sumsStatus = 200;
	bool createFails = false;
	bool writeFails = false;
	bool hashFails = false;
	bool launchFails = false;
	bool alive = false;
	bool sealed = false;
	bool wasSilent = false;
	int writes = 0;
	int launches = 0;
	int releases = 0;
	File* created = nullptr;
	std::string written;
	std::wstring createdName;
	QStringList urls;
};

CpuFeatures avx2Cpu()
{
	CpuFeatures features;
	features.avx = true;
	features.avx2 = true;
	return features;
}
}

TEST_CASE(EditorLogicTests, testAutoInstallerChannelMapping)
{
	// Most specific / newest first; ARM64 outranks everything; the exit-code
	// channel index rides along for --detect-only consumers.
	int index = -1;
	CpuFeatures features;
	expectEqual(wide(channelForCpu(features, &index)), QStringLiteral("x64-sse2"), "no features means sse2");
	expectEqual(index, int(kSse2), "sse2 index");

	features.avx = true;
	expectEqual(wide(channelForCpu(features, &index)), QStringLiteral("x64-avx"), "avx alone picks avx");
	expectEqual(index, int(kAvx), "avx index");

	features.avx2 = true;
	expectEqual(wide(channelForCpu(features, &index)), QStringLiteral("x64-avx2"), "avx2 outranks avx");
	expectEqual(index, int(kAvx2), "avx2 index");

	features.avx512f = true;
	expectEqual(wide(channelForCpu(features, &index)), QStringLiteral("x64-avx512"), "avx512 outranks avx2");
	expectEqual(index, int(kAvx512), "avx512 index");

	features.avx10_1 = true;
	expectEqual(wide(channelForCpu(features, &index)), QStringLiteral("x64-avx10-1"), "avx10.1 outranks avx512");
	expectEqual(index, int(kAvx10_1), "avx10.1 index");

	features.arm64Native = true;
	expectEqual(wide(channelForCpu(features, &index)), QStringLiteral("arm64-neon"), "arm64 outranks all x64 features");
	expectEqual(index, int(kArm64), "arm64 index");
}

TEST_CASE(EditorLogicTests, testAutoInstallerChannelDescriptions)
{
	// The window's "detected" line reads the same table channelForCpu does
	// (audit #348 F18): every channel the mapping can return has its own
	// description, and an unknown channel comes back verbatim.
	const auto describedFor = [](CpuFeatures features, const char* expected, const char* label)
	{
		const std::wstring channel = channelForCpu(features);
		expectEqual(wide(describeChannel(channel)), QString::fromLatin1(expected), label);
	};
	CpuFeatures features;
	describedFor(features, "64-bit x86 with SSE2", "sse2 description");
	features.avx = true;
	describedFor(features, "64-bit x86 with AVX", "avx description");
	features.avx2 = true;
	describedFor(features, "64-bit x86 with AVX2", "avx2 description");
	features.avx512f = true;
	describedFor(features, "64-bit x86 with AVX-512", "avx512 description");
	features.avx10_1 = true;
	describedFor(features, "64-bit x86 with AVX10.1", "avx10.1 description");
	features.arm64Native = true;
	describedFor(features, "ARM64 with NEON", "arm64 description");
	expectEqual(wide(describeChannel(L"riscv64-v")), QStringLiteral("riscv64-v"), "an unknown channel comes back verbatim");
}

TEST_CASE(EditorLogicTests, testAutoInstallerAssetGrammar)
{
	expectEqual(wide(assetName(L"x64-avx2")),
		QStringLiteral("EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe"),
		"asset name follows the shared grammar header");
	const QString url = wide(downloadUrl(L"arm64-neon"));
	expectTrue(url.startsWith(QStringLiteral("https://github.com/")),
		"download URL is absolute https");
	expectTrue(url.endsWith(QStringLiteral(
		"/releases/latest/download/EqualizerAPO-XT-arm64-neon-arm64-neon-Setup.exe")),
		"download URL uses the always-latest redirect path");
}

TEST_CASE(EditorLogicTests, testAutoInstallerChecksumParsing)
{
	const std::string hash(64, 'A');
	const std::string other(64, '2');
	const std::wstring name = L"EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe";

	// Plain line: digest comes back lowercased.
	std::string text = hash + "  EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe\n"
		+ other + "  SHA256SUMS.txt\n";
	expectEqual(wide(expectedHashFromChecksums(text, name)),
		QString(64, QLatin1Char('a')), "matching line yields the lowercased digest");

	// Binary-mode marker, CRLF endings, BOM, and case-insensitive names.
	text = "\xEF\xBB\xBF" + hash + " *equalizerapo-xt-x64-avx2-x64-avx2-setup.exe\r\n";
	expectEqual(wide(expectedHashFromChecksums(text, name)),
		QString(64, QLatin1Char('a')),
		"BOM, binary marker, CRLF and ASCII case are all tolerated");

	// Rejections: short hash, non-hex hash, absent name.
	text = std::string(63, 'a') + "  EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe\n";
	expectTrue(expectedHashFromChecksums(text, name).empty(), "63 hex digits do not parse");
	text = std::string(64, 'g') + "  EqualizerAPO-XT-x64-avx2-x64-avx2-Setup.exe\n";
	expectTrue(expectedHashFromChecksums(text, name).empty(), "non-hex digest does not parse");
	text = hash + "  some-other-asset.zip\n";
	expectTrue(expectedHashFromChecksums(text, name).empty(), "absent asset yields empty");
	expectTrue(expectedHashFromChecksums(std::string(), name).empty(), "empty text yields empty");
}

TEST_CASE(EditorLogicTests, testAutoInstallerVerifiedHeldFileFlow)
{
	InstallFixture fixture;
	const InstallResult result = install(avx2Cpu(), true, fixture.operations());
	expectTrue(result.stage == InstallStage::Complete, "the verified flow completes");
	expectEqual(static_cast<int>(result.exitCode), 37, "silent mode forwards the child exit code");
	expectTrue(fixture.wasSilent, "silent mode reaches the launcher");
	expectEqual(fixture.launches, 1, "the verified file is launched once");
	expectEqual(fixture.writes, 1, "only the installer body reaches the file, never checksums");
	expectEqual(fixture.releases, 1, "the held file is released after launch");
	expectFalse(fixture.alive, "the holder does not leak");
	expectEqual(wide(fixture.createdName), wide(assetName(L"x64-avx2")), "CPU detection chooses the file");
	requireEqual(static_cast<int>(fixture.urls.size()), 2, "exactly the installer and checksums are fetched");
	expectEqual(fixture.urls[0], wide(downloadUrl(L"x64-avx2")), "CPU detection chooses the URL");
	expectTrue(fixture.urls[1].endsWith(QStringLiteral("/SHA256SUMS.txt")), "checksums use the release asset");
}

TEST_CASE(EditorLogicTests, testAutoInstallerRejectsTamperedBody)
{
	InstallFixture fixture;
	fixture.body = "substituted installer";
	const InstallResult result = install(avx2Cpu(), false, fixture.operations());
	expectTrue(result.stage == InstallStage::Verify, "tampering fails verification");
	expectEqual(wide(result.error), QStringLiteral("The downloaded installer failed its integrity check."),
		"tampering reports the integrity error");
	expectEqual(fixture.launches, 0, "tampered bytes are never launched");
	expectEqual(fixture.releases, 1, "a rejected file is released for cleanup");
}

TEST_CASE(EditorLogicTests, testAutoInstallerHttpFailuresStopTheFlow)
{
	for (bool checksums : { false, true })
	{
		for (unsigned long status : { 404ul, 500ul })
		{
			InstallFixture fixture;
			(checksums ? fixture.sumsStatus : fixture.setupStatus) = status;
			const InstallResult result = install(avx2Cpu(), false, fixture.operations());
			expectTrue(result.stage == (checksums ? InstallStage::Verify : InstallStage::Download),
				"HTTP failure is assigned to the requested asset's step");
			expectEqual(static_cast<int>(result.httpStatus), static_cast<int>(status), "HTTP status is preserved");
			expectEqual(wide(result.error), wide(httpFailure(status)), "HTTP failure retains its real status");
			expectEqual(fixture.launches, 0, "HTTP failures never reach the launcher");
			expectEqual(fixture.writes, checksums ? 1 : 0, "only HTTP 200 installer bytes reach disk");
			expectEqual(fixture.releases, 1, "HTTP failure releases the held file");
		}
	}
}

TEST_CASE(EditorLogicTests, testAutoInstallerRejectsMissingAndOversizeChecksums)
{
	for (const std::string& text : { std::string(), std::string(1024 * 1024 + 1, 'a'),
		std::string(64, 'a') + "  other.exe\n" })
	{
		InstallFixture fixture;
		fixture.checksums = text;
		const InstallResult result = install(avx2Cpu(), false, fixture.operations());
		expectTrue(result.stage == InstallStage::Verify, "invalid checksums fail verification");
		expectFalse(result.error.empty(), "invalid checksums report an error");
		expectEqual(fixture.launches, 0, "invalid checksums never reach the launcher");
		expectEqual(fixture.writes, 1, "invalid checksums never touch disk");
		expectFalse(fixture.alive, "invalid checksums release the file");
	}
}

TEST_CASE(EditorLogicTests, testAutoInstallerFileAndLaunchFailures)
{
	InstallFixture create;
	create.createFails = true;
	expectTrue(install(avx2Cpu(), false, create.operations()).stage == InstallStage::Download,
		"file creation failure stops the download");
	expectTrue(create.urls.empty(), "no download without a destination");

	InstallFixture write;
	write.writeFails = true;
	expectTrue(install(avx2Cpu(), false, write.operations()).stage == InstallStage::Download,
		"write failure stops the download");
	expectEqual(write.launches, 0, "write failure never launches");
	expectFalse(write.alive, "write failure releases the file");

	InstallFixture hash;
	hash.hashFails = true;
	expectTrue(install(avx2Cpu(), false, hash.operations()).stage == InstallStage::Verify,
		"hash failure stops verification");
	expectEqual(hash.launches, 0, "hash failure never launches");
	expectFalse(hash.alive, "hash failure releases the file");

	InstallFixture launch;
	launch.launchFails = true;
	expectTrue(install(avx2Cpu(), false, launch.operations()).stage == InstallStage::Launch,
		"launch failure is reported at handoff");
	expectFalse(launch.alive, "launch failure releases the file");

	InstallFixture interactive;
	expectEqual(static_cast<int>(install(avx2Cpu(), false, interactive.operations()).exitCode), 0,
		"interactive success does not forward a child exit code");
	expectFalse(interactive.wasSilent, "interactive mode reaches the launcher");
}

TEST_CASE(EditorLogicTests, testAutoInstallerHttpFailure)
{
	expectEqual(wide(httpFailure(404)), QStringLiteral("The download server returned HTTP 404."),
		"HTTP 404 reports its status without assuming which asset was requested");
	expectEqual(wide(httpFailure(500)), QStringLiteral("The download server returned HTTP 500."),
		"HTTP 500 is a server failure, not a missing installer");
}

TEST_CASE(EditorLogicTests, testAutoInstallerFlagScan)
{
	wchar_t exe[] = L"setup.exe";
	wchar_t silent[] = L"/silent";
	wchar_t report[] = L"--report";
	wchar_t path[] = L"C:\\out.txt";
	wchar_t* argv[] = { exe, silent, report, path };

	expectTrue(hasFlag(4, argv, L"/silent"), "flag present");
	expectTrue(hasFlag(4, argv, L"/SILENT"), "flag match ignores case");
	expectFalse(hasFlag(4, argv, L"/detect-only"), "absent flag");
	expectFalse(hasFlag(1, argv, L"setup.exe"), "argv[0] is not a flag");
	expectEqual(wide(flagValue(4, argv, L"--report")), QStringLiteral("C:\\out.txt"),
		"flag value returns the following argument");
	expectTrue(flagValue(4, argv, L"C:\\out.txt") == nullptr,
		"a trailing token has no value slot");
}
