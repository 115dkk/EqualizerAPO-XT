#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "Editor/helpers/ConvolutionPathHelper.h"
#include "Editor/widgets/FilterCardModel.h"
#include "UpdateChecker/UpdateInfoFormatter.h"
#include "UpdateChecker/VelopackUpdateInfo.h"

namespace
{
QString normalized(QString path)
{
	return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

void fail(const QString& message)
{
	fprintf(stderr, "EditorLogicTests failed: %s\n", message.toUtf8().constData());
	exit(1);
}

void expectPath(const QString& actual, const QString& expected)
{
	if (normalized(actual) != normalized(expected))
		fail(QString("expected '%1', got '%2'").arg(expected, actual));
}

void expectTrue(bool value, const QString& message)
{
	if (!value)
		fail(message);
}

void expectFalse(bool value, const QString& message)
{
	if (value)
		fail(message);
}

void expectEqual(const QString& actual, const QString& expected, const QString& message)
{
	if (actual != expected)
		fail(QString("%1: expected '%2', got '%3'").arg(message, expected, actual));
}

void expectEqual(int actual, int expected, const QString& message)
{
	if (actual != expected)
		fail(QString("%1: expected %2, got %3").arg(message).arg(expected).arg(actual));
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QString configPath = "C:/EqualizerAPO/config/config.txt";

	expectPath(
		ConvolutionPathHelper::absolutePathForConfig(configPath, "irs/room.wav"),
		"C:/EqualizerAPO/config/irs/room.wav");
	expectPath(
		ConvolutionPathHelper::absolutePathForConfig(configPath, "C:/Impulse/room.wav"),
		"C:/Impulse/room.wav");

	expectPath(
		ConvolutionPathHelper::displayPathForSelection(configPath, "C:/EqualizerAPO/config/irs/room.wav"),
		"irs/room.wav");
	expectPath(
		ConvolutionPathHelper::displayPathForSelection(configPath, "C:/EqualizerAPO/shared/room.wav"),
		"C:/EqualizerAPO/shared/room.wav");
	expectPath(
		ConvolutionPathHelper::displayPathForSelection(configPath, "C:/Impulse/room.wav"),
		"C:/Impulse/room.wav");

	expectTrue(
		ConvolutionPathHelper::relativePathStaysInConfigDirectory("irs/room.wav"),
		"relative path inside config directory was rejected");
	expectFalse(
		ConvolutionPathHelper::relativePathStaysInConfigDirectory("../shared/room.wav"),
		"parent-directory relative path was accepted");
	expectFalse(
		ConvolutionPathHelper::relativePathStaysInConfigDirectory("C:/Impulse/room.wav"),
		"absolute path was accepted as relative");

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
	QJsonDocument githubReleaseDoc(githubRelease);

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

	FilterCardDescriptor preamp = FilterCardModel::describeLine("Preamp: -6 dB");
	expectEqual(preamp.badge, "PRE", "preamp card badge");
	expectEqual(preamp.title, "Preamp", "preamp card title");
	expectEqual(preamp.summary, "-6 dB", "preamp card summary");
	expectTrue(preamp.enabled, "preamp card was marked disabled");

	FilterCardDescriptor disabledFilter = FilterCardModel::describeLine("# Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71");
	expectFalse(disabledFilter.enabled, "commented filter was marked enabled");
	expectEqual(disabledFilter.badge, "PK", "disabled biquad badge");
	expectEqual(disabledFilter.title, "Biquad", "disabled biquad title");

	FilterCardDescriptor graphicEq = FilterCardModel::describeLine("GraphicEQ: 20 -1; 100 0; 1000 2");
	expectEqual(graphicEq.badge, "GEQ", "graphic eq badge");
	expectEqual(graphicEq.summary, "3 bands", "graphic eq band count");

	FilterCardDescriptor copy = FilterCardModel::describeLine("Copy: VL=L VR=R L=VL R=VR");
	expectEqual(copy.badge, "CPY", "copy card badge");
	expectEqual(copy.summary, "4 steps, 2 virtual", "copy card summary");
	expectTrue(copy.channelBadges.contains("L") && copy.channelBadges.contains("R"), "copy card did not expose final physical channels");

	FilterCardDescriptor channel = FilterCardModel::describeLine("Channel: L, R");
	expectEqual(channel.badge, "CH", "channel card badge");
	expectEqual(channel.summary, "L R", "channel card summary");
	expectTrue(channel.channelBadges.contains("L") && channel.channelBadges.contains("R"), "channel badges were not parsed");

	QVector<int> depths = FilterCardModel::calculateDepths(QList<QString>({
		"Channel: L R",
		"Preamp: -6 dB",
		"Include: nested.txt",
		"Delay: 10 ms",
		"Channel: ALL",
		"Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71"
	}));
	expectEqual(depths.size(), 6, "channel depth count");
	expectEqual(depths[0], 0, "channel command depth");
	expectEqual(depths[1], 1, "scoped preamp depth");
	expectEqual(depths[2], 1, "include depth");
	expectEqual(depths[3], 0, "include should reset channel depth");
	expectEqual(depths[4], 0, "channel all depth");
	expectEqual(depths[5], 0, "post channel-all depth");

	printf("EditorLogicTests passed\n");
	return 0;
}
