/*
    This file is part of EqualizerAPO-XT.
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
