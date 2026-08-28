/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonDocument>
#include <QString>

class VelopackUpdateInfo
{
public:
	static QString defaultChannel();
	static QString githubLatestReleaseUrl(const QString& repository);
	static QString feedFileName(const QString& channel);
	static QString feedAssetUrl(const QJsonDocument& githubReleaseDoc, const QString& channel);
	static bool isGitHubRelease(const QJsonDocument& doc);
	static bool isNewerVersion(const QString& candidateVersion, const QString& installedVersion);
	static QJsonDocument fromVelopackFeed(
		const QJsonDocument& feedDoc,
		const QJsonDocument& githubReleaseDoc,
		const QString& channel,
		const QString& installedVersion);
	static QJsonDocument fromGitHubRelease(
		const QJsonDocument& githubReleaseDoc,
		const QString& channel,
		const QString& installedVersion);
};
