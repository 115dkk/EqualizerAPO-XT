/*
	This file is part of EqualizerAPO-XT.

	The auto-detect installer's decision logic (audit #250 F056). The
	installer binary itself is built release-only, so until this split its
	channel mapping, asset grammar and checksum parsing were verified for
	the first time on release day. These tests compile the logic unit
	directly, the same pattern the suite uses for UpdateChecker's decision
	core.
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
}

void testAutoInstallerChannelMapping()
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

void testAutoInstallerAssetGrammar()
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

void testAutoInstallerChecksumParsing()
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

void testAutoInstallerFlagScan()
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
