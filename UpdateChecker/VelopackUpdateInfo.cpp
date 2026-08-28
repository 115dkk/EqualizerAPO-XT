/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "VelopackUpdateInfo.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <limits>

#include "release/ReleaseAssetNames.h"

namespace
{
QString normalizedVersion(QString version)
{
	version = version.trimmed();
	if (version.startsWith('v', Qt::CaseInsensitive))
		version.remove(0, 1);
	return version;
}

QVector<int> numericVersionParts(const QString& version)
{
	QVector<int> parts;
	QRegularExpression numberExpression("(\\d+)");
	QRegularExpressionMatchIterator it = numberExpression.globalMatch(version);
	while (it.hasNext())
	{
		// Audit #250 F044: toInt() returns 0 on overflow, which would make
		// an absurd component compare as the smallest instead of the
		// largest. Saturate explicitly.
		bool ok = false;
		const qlonglong value = it.next().captured(1).toLongLong(&ok);
		if (!ok || value > std::numeric_limits<int>::max())
			parts.append(std::numeric_limits<int>::max());
		else
			parts.append(static_cast<int>(value));
	}
	return parts;
}

QStringList releaseInfoLines(const QJsonObject& githubReleaseObj, const QString& channel, const QString& packageName = QString())
{
	QStringList lines;
	lines.append(QString("Velopack channel: %0").arg(channel));
	if (!packageName.isEmpty())
		lines.append(QString("Package: %0").arg(packageName));

	QString body = githubReleaseObj.value("body").toString();
	for (const QString& rawLine : body.split('\n'))
	{
		QString line = rawLine.trimmed();
		while (line.startsWith('#') || line.startsWith('*') || line.startsWith('-'))
			line = line.mid(1).trimmed();
		if (!line.isEmpty())
			lines.append(line);
	}

	if (lines.size() == 1)
	{
		QString name = githubReleaseObj.value("name").toString().trimmed();
		if (!name.isEmpty())
			lines.append(name);
	}

	return lines;
}

QJsonDocument makeUpdateDocument(
	const QString& downloadUrl,
	const QString& version,
	const QString& date,
	const QStringList& infoLines)
{
	if (downloadUrl.isEmpty() || version.isEmpty())
		return QJsonDocument();

	QJsonArray infoArray;
	for (const QString& line : infoLines)
		infoArray.append(line);

	QJsonObject versionObj;
	versionObj["version"] = normalizedVersion(version);
	versionObj["date"] = date.left(10);
	versionObj["info"] = infoArray;

	QJsonArray versions;
	versions.append(versionObj);

	QJsonObject docObj;
	docObj["download-url"] = downloadUrl;
	docObj["versions"] = versions;
	return QJsonDocument(docObj);
}

bool assetNameMatchesChannel(const QString& name, const QString& channel)
{
	QString lowerName = name.toLower();
	QString lowerChannel = channel.toLower();
	return lowerName.contains(lowerChannel) || lowerName.contains(QString(lowerChannel).replace('-', '_'));
}

QString assetUrlByName(const QJsonDocument& githubReleaseDoc, const QString& fileName)
{
	if (fileName.isEmpty())
		return QString();

	QJsonArray assets = githubReleaseDoc.object().value("assets").toArray();
	for (const QJsonValue& value : assets)
	{
		QJsonObject asset = value.toObject();
		if (asset.value("name").toString().compare(fileName, Qt::CaseInsensitive) == 0)
			return asset.value("browser_download_url").toString();
	}

	return QString();
}

QString setupAssetUrl(const QJsonDocument& githubReleaseDoc, const QString& channel)
{
	QString fallback;
	QJsonArray assets = githubReleaseDoc.object().value("assets").toArray();
	for (const QJsonValue& value : assets)
	{
		QJsonObject asset = value.toObject();
		QString name = asset.value("name").toString();
		QString lowerName = name.toLower();
		if (!assetNameMatchesChannel(name, channel))
			continue;

		QString url = asset.value("browser_download_url").toString();
		if (url.isEmpty())
			continue;
		if (lowerName.endsWith("-setup.exe") || lowerName.endsWith("setup.exe"))
			return url;
		if (fallback.isEmpty() && lowerName.endsWith(".exe"))
			fallback = url;
	}

	return fallback;
}

QString releasePageUrl(const QJsonDocument& githubReleaseDoc)
{
	return githubReleaseDoc.object().value("html_url").toString();
}

QJsonObject newestFeedAsset(const QJsonDocument& feedDoc, const QString& channel, const QString& installedVersion)
{
	const QString packageId = QString::fromStdWString(
		ReleaseAssetNames::velopackPackId(channel.toStdWString())).toLower();
	QJsonObject bestAsset;
	QString bestVersion;

	QJsonArray assets = feedDoc.object().value("Assets").toArray();
	for (const QJsonValue& value : assets)
	{
		QJsonObject asset = value.toObject();
		QString type = asset.value("Type").toString();
		QString package = asset.value("PackageId").toString();
		QString fileName = asset.value("FileName").toString();
		QString version = asset.value("Version").toString();
		if (version.isEmpty())
			continue;
		if (!type.isEmpty() && type.compare("Full", Qt::CaseInsensitive) != 0)
			continue;

		bool matchesPackage = package.toLower() == packageId;
		bool matchesFile = assetNameMatchesChannel(fileName, channel);
		if (!matchesPackage && !matchesFile)
			continue;
		if (!VelopackUpdateInfo::isNewerVersion(version, installedVersion))
			continue;
		if (bestVersion.isEmpty() || VelopackUpdateInfo::isNewerVersion(version, bestVersion))
		{
			bestAsset = asset;
			bestVersion = version;
		}
	}

	return bestAsset;
}
}

QString VelopackUpdateInfo::defaultChannel()
{
#ifdef EAPO_UPDATE_CHANNEL
	return QStringLiteral(EAPO_UPDATE_CHANNEL);
#elif defined(_M_ARM64) || defined(__aarch64__)
	return QStringLiteral("arm64-neon");
#else
	return QStringLiteral("x64-avx2");
#endif
}

QString VelopackUpdateInfo::githubLatestReleaseUrl(const QString& repository)
{
	QString repo = repository.trimmed();
	const QString githubPrefix = "https://github.com/";
	if (repo.startsWith(githubPrefix, Qt::CaseInsensitive))
		repo = repo.mid(githubPrefix.size());
	while (repo.startsWith('/'))
		repo.remove(0, 1);
	while (repo.endsWith('/'))
		repo.chop(1);

	return QString("https://api.github.com/repos/%0/releases/latest").arg(repo);
}

QString VelopackUpdateInfo::feedFileName(const QString& channel)
{
	return QString("releases.%0.json").arg(channel.toLower());
}

QString VelopackUpdateInfo::feedAssetUrl(const QJsonDocument& githubReleaseDoc, const QString& channel)
{
	const QString fileName = feedFileName(channel);
	QJsonArray assets = githubReleaseDoc.object().value("assets").toArray();
	for (const QJsonValue& value : assets)
	{
		QJsonObject asset = value.toObject();
		if (asset.value("name").toString().compare(fileName, Qt::CaseInsensitive) == 0)
			return asset.value("browser_download_url").toString();
	}

	return QString();
}

bool VelopackUpdateInfo::isGitHubRelease(const QJsonDocument& doc)
{
	QJsonObject obj = doc.object();
	return obj.value("assets").isArray()
		&& (!obj.value("tag_name").toString().isEmpty() || !obj.value("html_url").toString().isEmpty());
}

bool VelopackUpdateInfo::isNewerVersion(const QString& candidateVersion, const QString& installedVersion)
{
	QString candidate = normalizedVersion(candidateVersion);
	QString installed = normalizedVersion(installedVersion);
	if (candidate.isEmpty())
		return false;

	QVector<int> candidateParts = numericVersionParts(candidate);
	QVector<int> installedParts = numericVersionParts(installed);
	int count = std::max(candidateParts.size(), installedParts.size());
	for (int i = 0; i < count; ++i)
	{
		int candidatePart = i < candidateParts.size() ? candidateParts[i] : 0;
		int installedPart = i < installedParts.size() ? installedParts[i] : 0;
		if (candidatePart > installedPart)
			return true;
		if (candidatePart < installedPart)
			return false;
	}

	// Audit #250 F044: the zero-padded comparison above already judged the
	// versions equal; the old startsWith tie-breaker declared "v1.2.3.0"
	// forever newer than an installed "1.2.3" and offered the update on
	// every check.
	return false;
}

QJsonDocument VelopackUpdateInfo::fromVelopackFeed(
	const QJsonDocument& feedDoc,
	const QJsonDocument& githubReleaseDoc,
	const QString& channel,
	const QString& installedVersion)
{
	QJsonObject asset = newestFeedAsset(feedDoc, channel, installedVersion);
	if (asset.isEmpty())
		return QJsonDocument();

	QString fileName = asset.value("FileName").toString();
	QString downloadUrl = setupAssetUrl(githubReleaseDoc, channel);
	if (downloadUrl.isEmpty())
		downloadUrl = assetUrlByName(githubReleaseDoc, fileName);
	if (downloadUrl.isEmpty())
		downloadUrl = releasePageUrl(githubReleaseDoc);

	QString date = githubReleaseDoc.object().value("published_at").toString();
	return makeUpdateDocument(
		downloadUrl,
		asset.value("Version").toString(),
		date,
		releaseInfoLines(githubReleaseDoc.object(), channel, fileName));
}

QJsonDocument VelopackUpdateInfo::fromGitHubRelease(
	const QJsonDocument& githubReleaseDoc,
	const QString& channel,
	const QString& installedVersion)
{
	QJsonObject releaseObj = githubReleaseDoc.object();
	QString version = releaseObj.value("tag_name").toString();
	if (version.isEmpty())
		version = releaseObj.value("name").toString();
	if (!isNewerVersion(version, installedVersion))
		return QJsonDocument();

	QString downloadUrl = setupAssetUrl(githubReleaseDoc, channel);
	if (downloadUrl.isEmpty())
		downloadUrl = releasePageUrl(githubReleaseDoc);

	return makeUpdateDocument(
		downloadUrl,
		version,
		releaseObj.value("published_at").toString(),
		releaseInfoLines(releaseObj, channel));
}
