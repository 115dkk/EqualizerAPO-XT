/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LegacyMigrationPolicy.h"

#include <QDir>
#include <QRegularExpression>

namespace EqAPO::Import
{

QString LegacyMigrationPolicy::stableConfigRoot(const QString& localAppData)
{
    if (localAppData.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(localAppData + QStringLiteral("/EqualizerAPO-XT/config"));
}

bool LegacyMigrationPolicy::isVolatileXtConfigDir(const QString& dir)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    // ...\EqualizerAPO-XT-<variant>\current\config, any drive or profile.
    static const QRegularExpression volatilePattern(
        QStringLiteral("/EqualizerAPO-XT-[^/]+/current/config$"),
        QRegularExpression::CaseInsensitiveOption);
    return volatilePattern.match(clean).hasMatch();
}

bool LegacyMigrationPolicy::hasLegacyApoFolderName(const QString& dir)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(dir));
    static const QRegularExpression legacyPattern(
        QStringLiteral("/(EqualizerAPO|Equalizer APO)/config$"),
        QRegularExpression::CaseInsensitiveOption);
    return legacyPattern.match(clean).hasMatch();
}

QString LegacyMigrationPolicy::remapUnderRoot(const QString& path, const QString& fromRoot, const QString& toRoot)
{
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    const QString cleanFrom = QDir::cleanPath(QDir::fromNativeSeparators(fromRoot.trimmed()));
    if (cleanPath.isEmpty() || cleanFrom.isEmpty())
        return QString();
    if (cleanPath.length() <= cleanFrom.length()
        || !cleanPath.startsWith(cleanFrom, Qt::CaseInsensitive)
        || cleanPath.at(cleanFrom.length()) != QLatin1Char('/'))
        return QString();
    const QString rel = cleanPath.mid(cleanFrom.length() + 1);
    return QDir::cleanPath(QDir::fromNativeSeparators(toRoot) + QLatin1Char('/') + rel);
}

LegacyMigrationPolicy::Action LegacyMigrationPolicy::classify(const QString& existingConfigPath,
    const QString& stableRoot, bool legacyMarkersPresent, bool volatileXt)
{
    const QString existing = QDir::cleanPath(QDir::fromNativeSeparators(existingConfigPath.trimmed()));
    if (existing.isEmpty())
        return Action::AdoptStableRoot;

    const QString stable = QDir::cleanPath(QDir::fromNativeSeparators(stableRoot));
    if (QString::compare(existing, stable, Qt::CaseInsensitive) == 0)
        return Action::AlreadyOurs;

    if (volatileXt)
        return Action::MigrateVolatileXt;

    if (legacyMarkersPresent)
        return Action::MigrateLegacy;

    return Action::RespectCustom;
}

}
