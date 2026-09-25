/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "LegacyMigration.h"
#include "services/registry/RegistryPaths.h"
#include "services/settings/EditorSettings.h"
#include "LegacyMigrationPolicy.h"
#include "ConfigDependencyScanner.h"
#include "ImportExecutor.h"

#include "CallerProfileCheck.h"
#include "services/security/AudioEngineAccess.h"
#include "services/install/ApoRegistration.h"
#include "services/logging/Logging.h"
#include "services/registry/WindowsRegistry.h"

#include <cstdio>
#include <climits>
#include <optional>

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

// An empty string when the value is absent; nullopt when it is there but could
// not be read, which is logged here. The two used to be one answer, so a
// ConfigPath the hook failed to read looked unset and was overwritten with the
// stable root (audit #348 TD-49).
std::optional<QString> readRegistryString(const IRegistry& registry, const wchar_t* name)
{
    try
    {
        if (registry.keyExists(APP_REGPATH) && registry.valueExists(APP_REGPATH, name))
            return QString::fromStdWString(registry.readValue(APP_REGPATH, name));
        return QString();
    }
    catch (const RegistryError& e)
    {
        LogFStatic(L"Migration: could not read %s: %s", name, e.getMessage().c_str());
        return std::nullopt;
    }
}

// Copy every file below sourceDir into targetDir, keeping the relative
// layout and overwriting what is already there. Used to rescue a config
// tree out of a Velopack current\ dir, where the source is authoritative.
int copyTreeOverwriting(const QString& sourceDir, const QString& targetDir, bool* complete = nullptr)
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
        {
            if (complete)
                *complete = false;
            LogFStatic(L"Migration: failed to copy %s", reinterpret_cast<const wchar_t*>(sourceFile.utf16()));
        }
    }
    return copied;
}

// Ship the sample configs into the root without ever touching a file the
// user (or the migration) already put there.
bool seedMissingSamples(const QString& shippedConfigDir, const QString& targetDir)
{
    bool complete = true;
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
        if (!QFile::copy(sourceFile, targetFile))
            complete = false;
    }
    return complete;
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
    return LegacyMigrationPolicy::stableConfigRoot(qEnvironmentVariable("LOCALAPPDATA"));
}

QString LegacyMigration::configRoot(const IRegistry& registry)
{
    try
    {
        if (registry.keyExists(APP_REGPATH) && registry.valueExists(APP_REGPATH, L"ConfigPath"))
        {
            const QString configured = QString::fromStdWString(registry.readValue(APP_REGPATH, L"ConfigPath"));
            if (!configured.isEmpty())
                return configured;
        }
    }
    catch (const RegistryError& e)
    {
        LogFStatic(L"Could not read ConfigPath, using the stable config root: %s", e.getMessage().c_str());
    }
    const QString stableRoot = stableConfigRoot();
    if (!stableRoot.isEmpty() && QDir(stableRoot).exists())
        return stableRoot;
    return QDir::currentPath();
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

    const std::optional<QString> configured = readRegistryString(registry, L"ConfigPath");
    if (!configured)
    {
        // Whatever the value is, it may be a folder the user chose; the policy
        // leaves such a folder alone (RespectCustom), and so does a failed read.
        LogFStatic(L"Migration: ConfigPath could not be read, leaving it alone");
        return;
    }
    const QString existing = *configured;
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

std::wstring LegacyMigration::prepareHookStep(const std::wstring& exeDir, Handoff* details)
{
    return prepareHookStep(exeDir, systemRegistry(), details);
}

std::wstring LegacyMigration::prepareHookStep(const std::wstring& exeDir, const IRegistry& registry,
    Handoff* details)
{
    if (details)
        *details = {};
    if (AudioEngineAccess::isElevated())
        return L"failed";
    // Install permissions are independent of config migration: even a custom
    // ConfigPath needs the packaged DLL and Editor to be readable/executable.
    const QString packagedConfig = QDir(QString::fromStdWString(exeDir)).absoluteFilePath(QStringLiteral("config"));
    const bool installGranted = AudioEngineAccess::grantOwnedEngineAccess(exeDir) == AudioEngineAccess::Grant::Applied
        && QDir().mkpath(packagedConfig)
        && AudioEngineAccess::grantOwnedConfigAccess(QDir::toNativeSeparators(packagedConfig).toStdWString())
            == AudioEngineAccess::Grant::Applied;
    if (details)
        details->installGrantsPrepared = installGranted;
    if (!installGranted)
        LogFStatic(L"Migration prepare: install grants failed; no prepared-install flag will be sent");
    const QString root = stableConfigRoot();
    const auto configured = readRegistryString(registry, L"ConfigPath");
    if (root.isEmpty() || !configured)
        return L"failed";
    const auto action = LegacyMigrationPolicy::classify(*configured, root,
        looksLikeLegacyApoConfigDir(*configured), LegacyMigrationPolicy::isVolatileXtConfigDir(*configured));
    if (action == LegacyMigrationPolicy::Action::RespectCustom)
        return L"respect-custom";
    if (!QDir().mkpath(root)
        || AudioEngineAccess::grantOwnedConfigAccess(QDir::toNativeSeparators(root).toStdWString())
            != AudioEngineAccess::Grant::Applied)
        return L"failed";

    int copied = 0;
    std::wstring outcome = L"adopt";
    if (action == LegacyMigrationPolicy::Action::AlreadyOurs)
        outcome = L"already-ours";
    else if (action == LegacyMigrationPolicy::Action::MigrateVolatileXt)
    {
        bool complete = true;
        copied = copyTreeOverwriting(*configured, root, &complete);
        if (!complete)
            return L"failed";
        outcome = L"volatile";
    }
    else if (action == LegacyMigrationPolicy::Action::MigrateLegacy)
    {
        const QString config = QDir(*configured).absoluteFilePath(QStringLiteral("config.txt"));
        if (QFile::exists(config))
        {
            const auto manifest = ConfigDependencyScanner::scan(config, root, DestLayout::SourceFolderIsRoot);
            const auto result = ImportExecutor::execute(manifest, root);
            for (const QString& error : result.errors)
                LogFStatic(L"Migration prepare: %s", reinterpret_cast<const wchar_t*>(error.utf16()));
            if (!result.errors.isEmpty())
                return L"failed";
            copied = result.filesCopied;
        }
        outcome = L"legacy";
    }
    if (!seedMissingSamples(QDir(QString::fromStdWString(exeDir)).absoluteFilePath(QStringLiteral("config")), root))
        return L"failed";
    if (details && (outcome == L"legacy" || outcome == L"volatile"))
    {
        details->migratedFrom = configured->toStdWString();
        details->migratedFiles = std::to_wstring(copied);
    }
    return outcome;
}

LegacyMigration::Handoff LegacyMigration::parseHandoff(const std::vector<std::wstring>& arguments)
{
    Handoff result;
    for (size_t i = 0; i < arguments.size(); ++i)
    {
        if (arguments[i] == L"--caller-localappdata")
            result.localAppData = i + 1 < arguments.size() ? arguments[++i] : L"";
        else if (arguments[i] == L"--caller-migration-outcome")
            result.outcome = i + 1 < arguments.size() ? arguments[++i] : L"failed";
        else if (arguments[i] == L"--caller-migrated-from")
            result.migratedFrom = i + 1 < arguments.size() ? arguments[++i] : L"";
        else if (arguments[i] == L"--caller-migrated-files")
            result.migratedFiles = i + 1 < arguments.size() ? arguments[++i] : L"";
        else if (arguments[i] == L"--caller-install-grants-prepared")
            result.installGrantsPrepared = i + 1 < arguments.size() && arguments[++i] == L"1";
    }
    return result;
}

namespace
{
bool validBreadcrumbText(const std::wstring& value, size_t limit)
{
    if (value.size() > limit)
        return false;
    for (const wchar_t character : value)
    {
        if (character < 0x20 || (character >= 0x7f && character <= 0x9f))
            return false;
    }
    return true;
}

std::optional<int> breadcrumbCount(const std::wstring& value)
{
    // The original migration counts with int; cap before multiplication.
    if (value.empty() || value.size() > 10)
        return std::nullopt;
    int result = 0;
    for (const wchar_t digit : value)
    {
        if (digit < L'0' || digit > L'9' || result > (INT_MAX - (digit - L'0')) / 10)
            return std::nullopt;
        result = result * 10 + (digit - L'0');
    }
    return result;
}
}

bool LegacyMigration::recordPreparedHookStep(const Handoff& handoff, IRegistry& registry,
    const CallerProfileCheck::FileSystem* fileSystem)
{
    if (!handoff.localAppData)
        return false;
    std::wstring reason;
    const auto local = fileSystem
        ? CallerProfileCheck::verify(*handoff.localAppData, reason, *fileSystem)
        : CallerProfileCheck::verify(*handoff.localAppData, reason);
    if (!local)
    {
        LogFStatic(L"Migration record: caller rejected: %s", reason.c_str());
        return false;
    }
    if (!validBreadcrumbText(handoff.outcome, 32)
        || !validBreadcrumbText(handoff.migratedFrom, 4096)
        || !validBreadcrumbText(handoff.migratedFiles, 10))
    {
        LogFStatic(L"Migration record: invalid breadcrumb text; leaving registry unchanged");
        return true;
    }
    // Hints are not authority. Failed preparation never requests elevated
    // file work, and RespectCustom never requests a ConfigPath write.
    if (handoff.outcome == L"failed" || handoff.outcome == L"respect-custom")
        return true;
    const QString root = LegacyMigrationPolicy::stableConfigRoot(QString::fromStdWString(*local));
    const std::wstring native = QDir::toNativeSeparators(root).toStdWString();
    if (!CallerProfileCheck::verifyPreparedRoot(native, reason))
    {
        LogFStatic(L"Migration record: prepared root rejected: %s", reason.c_str());
        return true;
    }
    const auto configured = readRegistryString(registry, L"ConfigPath");
    if (!configured)
        return true;
    // Always re-derive from HKLM, including for unknown/missing outcomes.
    // A caller-supplied action cannot overwrite a concurrently chosen path.
    const auto action = LegacyMigrationPolicy::classify(*configured, root,
        looksLikeLegacyApoConfigDir(*configured), LegacyMigrationPolicy::isVolatileXtConfigDir(*configured));
    if (action == LegacyMigrationPolicy::Action::RespectCustom
        || action == LegacyMigrationPolicy::Action::AlreadyOurs)
        return true;
    const bool migrated = (action == LegacyMigrationPolicy::Action::MigrateLegacy && handoff.outcome == L"legacy")
        || (action == LegacyMigrationPolicy::Action::MigrateVolatileXt && handoff.outcome == L"volatile");
    const auto files = breadcrumbCount(handoff.migratedFiles);
    if (migrated && (handoff.migratedFrom.empty() || !files
        || QString::compare(QString::fromStdWString(handoff.migratedFrom), *configured, Qt::CaseInsensitive) != 0))
    {
        LogFStatic(L"Migration record: invalid or stale migration metadata; leaving registry unchanged");
        return true;
    }
    try
    {
        registry.writeValue(APP_REGPATH, L"ConfigPath", native);
        // Display metadata only in this elevated step: never pass the supplied
        // source to a file API. The stamp is generated here, just as in fallback.
        if (migrated)
            writeMigrationBreadcrumbs(registry, QString::fromStdWString(handoff.migratedFrom), *files);
    }
    catch (const RegistryError& e)
    {
        LogFStatic(L"Migration record: ConfigPath write failed: %s", e.getMessage().c_str());
    }
    return true;
}

void LegacyMigration::runElevatedHookStep(const std::wstring& exeDir, const Handoff& handoff)
{
    if (recordPreparedHookStep(handoff, systemRegistry()))
        return;
    LogFStatic(L"Migration: no valid caller profile; using the elevated process profile");
    runElevatedHookStep(exeDir);
}

int LegacyMigration::dryRun()
{
	const QString stableRoot = stableConfigRoot();
	const std::optional<QString> configured = readRegistryString(systemRegistry(), L"ConfigPath");
	const QString existing = configured.value_or(QString());
	const bool legacyMarkers = looksLikeLegacyApoConfigDir(existing);
	const bool volatileXt = LegacyMigrationPolicy::isVolatileXtConfigDir(existing);
	// The hook leaves a ConfigPath it cannot read alone; report the same.
	const LegacyMigrationPolicy::Action action = configured
		? LegacyMigrationPolicy::classify(existing, stableRoot, legacyMarkers, volatileXt)
		: LegacyMigrationPolicy::Action::RespectCustom;

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
		!configured ? L"(could not be read)"
		: existing.isEmpty() ? L"(absent)" : reinterpret_cast<const wchar_t*>(existing.utf16()));
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
	const QString migratedFrom = readRegistryString(systemRegistry(), L"MigratedFrom").value_or(QString());
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
	// new path already accumulated.
	QSettings settings(QString::fromWCharArray(EDITOR_PER_FILE_REGPATH), QSettings::NativeFormat);
	const QString oldGroup = EditorSettings::perFileGroup(path);
	const QString newGroup = EditorSettings::perFileGroup(remapped);
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
    const QString stamp = readRegistryString(systemRegistry(), L"MigrationStamp").value_or(QString());
    if (stamp.isEmpty())
        return;

    QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
    if (settings.value(QStringLiteral("interface/migrationNoticeShown")).toString() == stamp)
        return;

    const QString from = readRegistryString(systemRegistry(), L"MigratedFrom").value_or(QString());
    const QString root = stableConfigRoot();
    QMessageBox::information(parent, QObject::tr("Configuration folder moved"),
        QObject::tr("Your Equalizer APO configuration was imported into the EqualizerAPO-XT "
                    "configuration folder:\n\n%1\n\nThis folder is now the one the audio "
                    "pipeline reads. The previous folder is no longer used:\n\n%2")
        .arg(QDir::toNativeSeparators(root), QDir::toNativeSeparators(from)));

    settings.setValue(QStringLiteral("interface/migrationNoticeShown"), stamp);
}

}
