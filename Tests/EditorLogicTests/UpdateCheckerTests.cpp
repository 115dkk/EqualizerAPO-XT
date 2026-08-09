/*
	This file is part of EqualizerAPO-XT.

	Update checking: the release-notes HTML formatter and the Velopack
	update feed conversion, including the GitHub release fallback both
	feed paths share.
*/

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "UpdateChecker/UpdateInfoFormatter.h"
#include "UpdateChecker/VelopackUpdateInfo.h"

#include "EditorLogicTestSupport.h"

void testUpdateInfoFormatter()
{
	QJsonObject release;
	release["download-url"] = "https://example.invalid";
	QJsonArray versions;
	QJsonObject version;
	version["version"] = "<b>2.0</b>";
	version["date"] = "not-a-date";
	version["info"] = QJsonArray({ "<script>alert(1)</script>", "Fix & verify" });
	versions.append(version);
	release["versions"] = versions;

	QString newestVersion;
	QString html = UpdateInfoFormatter::releaseHtml(QJsonDocument(release), &newestVersion);
	if (newestVersion != "<b>2.0</b>")
		fail("newest version was not preserved");
	if (html.contains("<script>") || html.contains("<b>2.0</b>") || html.contains("Fix & verify"))
		fail("release notes were not HTML-escaped");
	if (!html.contains("&lt;script&gt;alert(1)&lt;/script&gt;") || !html.contains("Fix &amp; verify"))
		fail("escaped release notes are missing");
}

void testVelopackUpdateInfo()
{
	expectEqual(
		VelopackUpdateInfo::githubLatestReleaseUrl("https://github.com/115dkk/EqualizerAPO-XT/"),
		"https://api.github.com/repos/115dkk/EqualizerAPO-XT/releases/latest",
		"GitHub latest release URL");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-avx2"),
		"releases.x64-avx2.json",
		"Velopack feed file name");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-sse2"),
		"releases.x64-sse2.json",
		"Velopack SSE2 feed file name");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-avx"),
		"releases.x64-avx.json",
		"Velopack AVX feed file name");
	expectTrue(
		VelopackUpdateInfo::isNewerVersion("v1.4.4-main.77", "1.4.3"),
		"Velopack package version was not considered newer");
	expectTrue(
		VelopackUpdateInfo::isNewerVersion("1.4.3-main.77", "1.4.3"),
		"CI package version was not considered newer than the base installed version");
	expectFalse(
		VelopackUpdateInfo::isNewerVersion("1.4.0", "1.4.3"),
		"older package version was considered newer");
}

// Builds the GitHub release fixture shared by testVelopackGitHubRelease() and
// testVelopackFeeds(): the feed conversion takes the release document as its
// fallback source, so both blocks work on the identical document.
namespace
{
QJsonDocument makeGitHubReleaseDoc()
{
	QJsonObject githubRelease;
	githubRelease["tag_name"] = "v1.4.4-main.77";
	githubRelease["name"] = "EqualizerAPO-XT 1.4.4-main.77";
	githubRelease["html_url"] = "https://github.com/115dkk/EqualizerAPO-XT/releases/tag/v1.4.4-main.77";
	githubRelease["published_at"] = "2026-05-22T00:00:00Z";
	githubRelease["body"] = "Fix convolution updates\nShip Velopack feeds";
	QJsonArray releaseAssets;
	releaseAssets.append(QJsonObject({
		{ "name", "releases.x64-avx2.json" },
		{ "browser_download_url", "https://example.invalid/releases.x64-avx2.json" },
	}));
	releaseAssets.append(QJsonObject({
		{ "name", "EqualizerAPO-XT-x64-avx2-1.4.4-main.77-full.nupkg" },
		{ "browser_download_url", "https://example.invalid/full.nupkg" },
	}));
	releaseAssets.append(QJsonObject({
		{ "name", "EqualizerAPO-XT-x64-avx2-Setup.exe" },
		{ "browser_download_url", "https://example.invalid/setup.exe" },
	}));
	githubRelease["assets"] = releaseAssets;
	return QJsonDocument(githubRelease);
}
}

void testVelopackGitHubRelease()
{
	QJsonDocument githubReleaseDoc = makeGitHubReleaseDoc();

	expectTrue(
		VelopackUpdateInfo::isGitHubRelease(githubReleaseDoc),
		"GitHub release response was not detected");
	expectEqual(
		VelopackUpdateInfo::feedAssetUrl(githubReleaseDoc, "x64-avx2"),
		"https://example.invalid/releases.x64-avx2.json",
		"Velopack feed asset URL");
	expectEqual(
		VelopackUpdateInfo::fromGitHubRelease(githubReleaseDoc, "x64-avx2", "1.4.3").object().value("download-url").toString(),
		"https://example.invalid/setup.exe",
		"GitHub release fallback setup URL");
}

void testVelopackFeeds()
{
	QJsonDocument githubReleaseDoc = makeGitHubReleaseDoc();

	QJsonArray feedAssets;
	feedAssets.append(QJsonObject({
		{ "PackageId", "EqualizerAPO-XT-x64-avx2" },
		{ "Version", "1.4.4-main.77" },
		{ "Type", "Full" },
		{ "FileName", "EqualizerAPO-XT-x64-avx2-1.4.4-main.77-full.nupkg" },
	}));
	feedAssets.append(QJsonObject({
		{ "PackageId", "EqualizerAPO-XT-x64-avx512" },
		{ "Version", "1.4.5-main.1" },
		{ "Type", "Full" },
		{ "FileName", "EqualizerAPO-XT-x64-avx512-1.4.5-main.1-full.nupkg" },
	}));
	QJsonDocument feedDoc(QJsonObject({ { "Assets", feedAssets } }));
	QJsonDocument updateDoc = VelopackUpdateInfo::fromVelopackFeed(feedDoc, githubReleaseDoc, "x64-avx2", "1.4.3");
	QJsonObject updateObj = updateDoc.object();
	expectEqual(
		updateObj.value("download-url").toString(),
		"https://example.invalid/setup.exe",
		"Velopack setup URL");
	QJsonObject velopackVersion = updateObj.value("versions").toArray().first().toObject();
	expectEqual(
		velopackVersion.value("version").toString(),
		"1.4.4-main.77",
		"Velopack update version");
	QStringList infoLines;
	for (const QJsonValue& infoValue : velopackVersion.value("info").toArray())
		infoLines.append(infoValue.toString());
	expectTrue(
		infoLines.contains("Velopack channel: x64-avx2") && infoLines.contains("Fix convolution updates"),
		"Velopack release notes were not preserved");
	expectTrue(
		VelopackUpdateInfo::fromVelopackFeed(feedDoc, githubReleaseDoc, "x64-avx2", "1.4.4-main.77").isEmpty(),
		"same Velopack version produced an update");
}
