/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LegacyMigration.h"
#include "services/registry/RegistryPaths.h"
#include "LegacyMigrationPolicy.h"
#include "ConfigDependencyScanner.h"
#include "ImportExecutor.h"

#include "services/install/ApoRegistration.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"

#include <cstdio>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QString>

namespace EqAPO::Import
{

namespace
{

QString readRegistryString(const IRegistry& registry, const wchar_t* name)
{
    try
    {
        if (registry.valueExists(APP_REGPATH, name))
            return QString::fromStdWString(registry.readValue(APP_REGPATH, name));
    }
    catch (const RegistryError&)
    {
    }
    return QString();
}

// Copy every file below sourceDir into targetDir, keeping the relative
// layout and overwriting what is already there. Used to rescue a config
// tree out of a Velopack current\ dir, where the source is authoritative.
int copyTreeOverwriting(const QString& sourceDir, const QString& targetDir)
{
    int copied = 0;
    QDir source(sourceDir);
    QDirIterator it(sourceDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString sourceFile = it.next();
        const QString rel = source.relativeFilePath(sourceFile);
        const QString targetFile = QDir(targetDir).absoluteFilePath(rel);
        QDir().mkpath(QFileInfo(targetFile).absolutePath());
        if (QFile::exists(targetFile))
            QFile::remove(targetFile);
        if (QFile::copy(sourceFile, targetFile))
            copied++;
        else
            LogFStatic(L"Migration: failed to copy %s", reinterpret_cast<const wchar_t*>(sourceFile.utf16()));
    }
    return copied;
}

// Ship the sample configs into the root without ever touching a file the
// user (or the migration) already put there.
void seedMissingSamples(const QString& shippedConfigDir, const QString& targetDir)
{
    QDirIterator it(shippedConfigDir, QDir::Files, QDirIterator::Subdirectories);
    QDir shipped(shippedConfigDir);
    while (it.hasNext())
    {
        const QString sourceFile = it.next();
        const QString rel = shipped.relativeFilePath(sourceFile);
        const QString targetFile = QDir(targetDir).absoluteFilePath(rel);
        if (QFile::exists(targetFile))
            continue;
        QDir().mkpath(QFileInfo(targetFile).absolutePath());
        QFile::copy(sourceFile, targetFile);
    }
}

void writeMigrationBreadcrumbs(IRegistry& registry, const QString& from, int filesCopied)
{
    registry.writeValue(APP_REGPATH, L"MigratedFrom", from.toStdWString());
    registry.writeValue(APP_REGPATH, L"MigrationStamp",
        QDateTime::currentDateTime().toString(Qt::ISODate).toStdWString());
    registry.writeDWORDValue(APP_REGPATH, L"MigratedFiles", static_cast<unsigned long>(filesCopied));
}

}

QString LegacyMigration::stableConfigRoot()
{
    return LegacyMigrationPolicy::stableConfigRoot(
        QString::fromLocal8Bit(qgetenv("LOCALAPPDATA")));
}

bool LegacyMigration::looksLikeLegacyApoConfigDir(const QString& configDir)
{
    const QString clean = QDir::cleanPath(configDir);
    if (clean.isEmpty() || !QDir(clean).exists())
        return false;
    // The installer's folder name is enough: uninstalling the legacy APO
    // deletes its binaries but leaves the config folder and the stale
    // ConfigPath value behind, so binary markers cannot be required.
    if (LegacyMigrationPolicy::hasLegacyApoFolderName(clean))
        return true;
    QDir parent(clean);
    if (!parent.cdUp())
        return false;
    return QFile::exists(parent.absoluteFilePath(QStringLiteral("EqualizerAPO.dll")))
        || QFile::exists(parent.absoluteFilePath(QStringLiteral("Uninstall.exe")));
}

void LegacyMigration::runElevatedHookStep(const std::wstring& exeDir)
{
    runElevatedHookStep(exeDir, systemRegistry());
}

void LegacyMigration::runElevatedHookStep(const std::wstring& exeDir, IRegistry& registry)
{
    const QString stableRoot = stableConfigRoot();
    if (stableRoot.isEmpty())
    {
        LogFStatic(L"Migration: LOCALAPPDATA missing, keeping existing ConfigPath");
        return;
    }

    const QString existing = readRegistryString(registry, L"ConfigPath");
    const LegacyMigrationPolicy::Action action = LegacyMigrationPolicy::classify(
        existing, stableRoot,
        looksLikeLegacyApoConfigDir(existing),
        LegacyMigrationPolicy::isVolatileXtConfigDir(existing));

    if (action == LegacyMigrationPolicy::Action::RespectCustom)
    {
        LogFStatic(L"Migration: ConfigPath %s is user-chosen, leaving it alone",
            reinterpret_cast<const wchar_t*>(existing.utf16()));
        return;
    }

    QDir().mkpath(stableRoot);
    // audiodg (LOCAL SERVICE) must read configs here and the user must be
    // able to edit them; same grants the install() hook applies to the
    // packaged config dir.
    ApoRegistration::secureConfigDir(QDir::toNativeSeparators(stableRoot).toStdWString());

    try
    {
        switch (action)
        {
        case LegacyMigrationPolicy::Action::AlreadyOurs:
            break;
        case LegacyMigrationPolicy::Action::AdoptStableRoot:
            registry.writeValue(APP_REGPATH, L"ConfigPath",
                QDir::toNativeSeparators(stableRoot).toStdWString());
            LogFStatic(L"Migration: ConfigPath adopted %s",
                reinterpret_cast<const wchar_t*>(stableRoot.utf16()));
            break;
        case LegacyMigrationPolicy::Action::MigrateLegacy:
        {
            // Bring over exactly what the legacy config.txt reaches: Include
            // chains at any depth, convolution IRs, VST references, with the
            // legacy folder mapped 1:1 onto the stable root. Unreferenced
            // files stay behind; absolute references outside the legacy root
            // keep working unmoved.
            const QString legacyConfigTxt = QDir(existing).absoluteFilePath(QStringLiteral("config.txt"));
            int copied = 0;
            if (QFile::exists(legacyConfigTxt))
            {
                const ImportManifest manifest = ConfigDependencyScanner::scan(
                    legacyConfigTxt, stableRoot, DestLayout::SourceFolderIsRoot);
                for (const QString& warning : manifest.warnings)
                    LogFStatic(L"Migration: %s", reinterpret_cast<const wchar_t*>(warning.utf16()));
                const ExecutionResult result = ImportExecutor::execute(manifest, stableRoot);
                for (const QString& error : result.errors)
                    LogFStatic(L"Migration: %s", reinterpret_cast<const wchar_t*>(error.utf16()));
                copied = result.filesCopied;
            }
            else
            {
                LogFStatic(L"Migration: legacy dir %s has no config.txt, repointing without import",
                    reinterpret_cast<const wchar_t*>(existing.utf16()));
            }
            registry.writeValue(APP_REGPATH, L"ConfigPath",
                QDir::toNativeSeparators(stableRoot).toStdWString());
            writeMigrationBreadcrumbs(registry, existing, copied);
            LogFStatic(L"Migration: imported %d file(s) from legacy %s",
                copied, reinterpret_cast<const wchar_t*>(existing.utf16()));
            break;
        }
        case LegacyMigrationPolicy::Action::MigrateVolatileXt:
        {
            // Everything in a current\config dir is one update away from
            // deletion (Velopack recreates current\ wholesale), so the whole
            // folder is rescued, not just what config.txt references.
            const int copied = QDir(existing).exists()
                ? copyTreeOverwriting(existing, stableRoot) : 0;
            registry.writeValue(APP_REGPATH, L"ConfigPath",
                QDir::toNativeSeparators(stableRoot).toStdWString());
            writeMigrationBreadcrumbs(registry, existing, copied);
            LogFStatic(L"Migration: rescued %d file(s) from volatile %s",
                copied, reinterpret_cast<const wchar_t*>(existing.utf16()));
            break;
        }
        default:
            break;
        }
    }
    catch (const RegistryError& e)
    {
        LogFStatic(L"Migration: registry write failed: %s", e.getMessage().c_str());
        return;
    }

    seedMissingSamples(
        QDir(QString::fromStdWString(exeDir)).absoluteFilePath(QStringLiteral("config")),
        stableRoot);
}

int LegacyMigration::dryRun()
{
	const QString stableRoot = stableConfigRoot();
	const QString existing = readRegistryString(systemRegistry(), L"ConfigPath");
	const bool legacyMarkers = looksLikeLegacyApoConfigDir(existing);
	const bool volatileXt = LegacyMigrationPolicy::isVolatileXtConfigDir(existing);
	const LegacyMigrationPolicy::Action action = LegacyMigrationPolicy::classify(
		existing, stableRoot, legacyMarkers, volatileXt);

	const char* actionName = "?";
	switch (action)
	{
	case LegacyMigrationPolicy::Action::AdoptStableRoot: actionName = "AdoptStableRoot"; break;
	case LegacyMigrationPolicy::Action::AlreadyOurs: actionName = "AlreadyOurs"; break;
	case LegacyMigrationPolicy::Action::MigrateLegacy: actionName = "MigrateLegacy"; break;
	case LegacyMigrationPolicy::Action::MigrateVolatileXt: actionName = "MigrateVolatileXt"; break;
	case LegacyMigrationPolicy::Action::RespectCustom: actionName = "RespectCustom"; break;
	}

	fwprintf(stderr, L"[migration dry-run] ConfigPath: %s\n",
		existing.isEmpty() ? L"(absent)" : reinterpret_cast<const wchar_t*>(existing.utf16()));
	fwprintf(stderr, L"[migration dry-run] stable root: %s\n",
		reinterpret_cast<const wchar_t*>(stableRoot.utf16()));
	fwprintf(stderr, L"[migration dry-run] legacy markers: %d, volatile XT dir: %d\n",
		legacyMarkers ? 1 : 0, volatileXt ? 1 : 0);
	fwprintf(stderr, L"[migration dry-run] action: %S\n", actionName);

	if (action == LegacyMigrationPolicy::Action::MigrateLegacy)
	{
		const QString legacyConfigTxt = QDir(existing).absoluteFilePath(QStringLiteral("config.txt"));
		if (!QFile::exists(legacyConfigTxt))
		{
			fwprintf(stderr, L"[migration dry-run] no config.txt in the legacy dir; would repoint without import\n");
			return 0;
		}
		const ImportManifest manifest = ConfigDependencyScanner::scan(
			legacyConfigTxt, stableRoot, DestLayout::SourceFolderIsRoot);
		for (const ImportItem& item : manifest.items)
			fwprintf(stderr, L"[migration dry-run] %s%s -> %s (%lld bytes)\n",
				item.exists ? L"" : L"MISSING ",
				reinterpret_cast<const wchar_t*>(item.sourceAbsolute.utf16()),
				reinterpret_cast<const wchar_t*>(item.destRelative.utf16()),
				static_cast<long long>(item.sizeBytes));
		for (const QString& warning : manifest.warnings)
			fwprintf(stderr, L"[migration dry-run] warning: %s\n",
				reinterpret_cast<const wchar_t*>(warning.utf16()));
		fwprintf(stderr, L"[migration dry-run] %d file(s), %lld bytes total\n",
			int(manifest.items.size()), static_cast<long long>(manifest.totalBytes));
	}
	return 0;
}

QString LegacyMigration::adoptMigratedFile(const QString& path)
{
	const QString migratedFrom = readRegistryString(systemRegistry(), L"MigratedFrom");
	if (migratedFrom.isEmpty())
		return path;

	const QString stableRoot = stableConfigRoot();
	const QString remapped = LegacyMigrationPolicy::remapUnderRoot(path, migratedFrom, stableRoot);
	if (remapped.isEmpty())
		return path;

	if (!QFile::exists(remapped))
	{
		// The referenced-set import only carried what config.txt reaches; a
		// tab the user kept open on some other file is still worth keeping
		// alive. Copy it over on demand — the legacy original stays behind.
		if (!QFile::exists(path))
			return path;
		QDir().mkpath(QFileInfo(remapped).absolutePath());
		if (!QFile::copy(path, remapped))
			return path;
	}

	// Row prefs and scroll offsets are keyed by the absolute path; port them
	// so the remapped tab keeps its per-file state. Never overwrite prefs the
	// new path already accumulated. The key mirrors EDITOR_PER_FILE_REGPATH
	// (MainWindow.h) without pulling the whole MainWindow header in here.
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH L"\\file-specific"), QSettings::NativeFormat);
	const QString oldGroup = QDir::toNativeSeparators(path).replace(QLatin1Char('\\'), QLatin1Char('|'));
	const QString newGroup = QDir::toNativeSeparators(remapped).replace(QLatin1Char('\\'), QLatin1Char('|'));
	settings.beginGroup(newGroup);
	const bool newGroupEmpty = settings.allKeys().isEmpty();
	settings.endGroup();
	if (newGroupEmpty)
	{
		settings.beginGroup(oldGroup);
		const QStringList keys = settings.allKeys();
		QVariantList values;
		for (const QString& key : keys)
			values.append(settings.value(key));
		settings.endGroup();
		settings.beginGroup(newGroup);
		for (int i = 0; i < keys.size(); i++)
			settings.setValue(keys[i], values[i]);
		settings.endGroup();
	}

	return QDir::toNativeSeparators(remapped);
}

void LegacyMigration::maybeShowStartupNotice(QWidget* parent)
{
    const QString stamp = readRegistryString(systemRegistry(), L"MigrationStamp");
    if (stamp.isEmpty())
        return;

    QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
    if (settings.value(QStringLiteral("interface/migrationNoticeShown")).toString() == stamp)
        return;

    const QString from = readRegistryString(systemRegistry(), L"MigratedFrom");
    const QString root = stableConfigRoot();
    QMessageBox::information(parent, QObject::tr("Configuration folder moved"),
        QObject::tr("Your Equalizer APO configuration was imported into the EqualizerAPO-XT "
                    "configuration folder:\n\n%1\n\nThis folder is now the one the audio "
                    "pipeline reads. The previous folder is no longer used:\n\n%2")
        .arg(QDir::toNativeSeparators(root), QDir::toNativeSeparators(from)));

    settings.setValue(QStringLiteral("interface/migrationNoticeShown"), stamp);
}

}
