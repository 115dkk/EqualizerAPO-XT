/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2026  115dkk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "EditorLogicTestSupport.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include "Editor/import/LegacyMigration.h"
#include "services/registry/RegistryPaths.h"
#include "Tests/FakeRegistry.h"

namespace
{
// The hook resolves its target root from LOCALAPPDATA; point it at a
// temporary directory for the duration of a test.
class ScopedLocalAppData
{
public:
	explicit ScopedLocalAppData(const QString& path)
		: previous(qgetenv("LOCALAPPDATA"))
	{
		qputenv("LOCALAPPDATA", QDir::toNativeSeparators(path).toUtf8());
	}

	~ScopedLocalAppData()
	{
		qputenv("LOCALAPPDATA", previous);
	}

private:
	QByteArray previous;
};
}

// Audit #275 C1: the migration hook's machine-changing registry writes used to
// bypass the port (static WindowsRegistry calls), so the only machine they
// could ever be observed on was a real one. Through the port they run against
// a fake here.
void testLegacyMigrationHookAdoptsStableRootThroughThePort()
{
	QTemporaryDir tempRoot;
	requireTrue(tempRoot.isValid(), QStringLiteral("temp LOCALAPPDATA root created"));
	ScopedLocalAppData scopedEnv(tempRoot.path());

	QTemporaryDir exeDir;
	requireTrue(exeDir.isValid(), QStringLiteral("temp exe dir created"));

	test::FakeRegistry registry;
	registry.seedKey(APP_REGPATH);

	EqAPO::Import::LegacyMigration::runElevatedHookStep(
		QDir::toNativeSeparators(exeDir.path()).toStdWString(), registry);

	requireTrue(registry.valueExists(APP_REGPATH, L"ConfigPath"),
		QStringLiteral("the hook writes ConfigPath through the injected registry"));
	const QString written = QString::fromStdWString(
		registry.readValue(APP_REGPATH, L"ConfigPath"));
	const QString expected = QDir::toNativeSeparators(
		tempRoot.path() + QStringLiteral("/EqualizerAPO-XT/config"));
	expectEqual(written, expected,
		QStringLiteral("ConfigPath adopts the stable per-user root"));
	expectTrue(QDir(expected).exists(),
		QStringLiteral("the stable config root directory is created"));
	// A fresh adoption is not a migration: no breadcrumbs.
	expectFalse(registry.valueExists(APP_REGPATH, L"MigratedFrom"),
		QStringLiteral("adopting an empty ConfigPath leaves no migration breadcrumbs"));
}

// Audit #348 TD-49: a ConfigPath the hook could not read used to come back as
// an empty string, which the policy takes for "no ConfigPath" and answers by
// adopting the stable root - overwriting a value that may be a folder the user
// chose. A DWORD under the name makes valueExists true and readValue throw.
void testLegacyMigrationHookLeavesAnUnreadableConfigPathAlone()
{
	QTemporaryDir tempRoot;
	requireTrue(tempRoot.isValid(), QStringLiteral("temp LOCALAPPDATA root created"));
	ScopedLocalAppData scopedEnv(tempRoot.path());

	QTemporaryDir exeDir;
	requireTrue(exeDir.isValid(), QStringLiteral("temp exe dir created"));

	test::FakeRegistry registry;
	registry.seedKey(APP_REGPATH);
	registry.seedDword(APP_REGPATH, L"ConfigPath", 1);

	EqAPO::Import::LegacyMigration::runElevatedHookStep(
		QDir::toNativeSeparators(exeDir.path()).toStdWString(), registry);

	expectEqual(static_cast<int>(registry.readDWORDValue(APP_REGPATH, L"ConfigPath")), 1,
		QStringLiteral("a ConfigPath that could not be read is left as it was"));
	expectFalse(QDir(tempRoot.path() + QStringLiteral("/EqualizerAPO-XT/config")).exists(),
		QStringLiteral("a ConfigPath that could not be read does not get the stable root created for it"));
	expectFalse(registry.valueExists(APP_REGPATH, L"MigratedFrom"),
		QStringLiteral("a ConfigPath that could not be read leaves no migration breadcrumbs"));
}

void testLegacyMigrationHookRescuesVolatileTreeAndLeavesBreadcrumbs()
{
	QTemporaryDir tempRoot;
	requireTrue(tempRoot.isValid(), QStringLiteral("temp LOCALAPPDATA root created"));
	ScopedLocalAppData scopedEnv(tempRoot.path());

	// A Velopack-style volatile config dir: <install>\current\config, one
	// update away from deletion.
	QTemporaryDir installBase;
	requireTrue(installBase.isValid(), QStringLiteral("temp install root created"));
	// The volatile classifier keys on the Velopack install folder shape:
	// ...\EqualizerAPO-XT-<variant>\current\config.
	const QString installRoot = installBase.path() + QStringLiteral("/EqualizerAPO-XT-x64-avx2");
	const QString volatileConfig = installRoot + QStringLiteral("/current/config");
	requireTrue(QDir().mkpath(volatileConfig), QStringLiteral("volatile config dir created"));
	{
		QFile file(volatileConfig + QStringLiteral("/config.txt"));
		requireTrue(file.open(QIODevice::WriteOnly), QStringLiteral("volatile config.txt written"));
		file.write("Preamp: -3 dB\n");
	}

	test::FakeRegistry registry;
	registry.seedKey(APP_REGPATH);
	registry.seedString(APP_REGPATH, L"ConfigPath",
		QDir::toNativeSeparators(volatileConfig).toStdWString());

	EqAPO::Import::LegacyMigration::runElevatedHookStep(
		QDir::toNativeSeparators(installRoot + QStringLiteral("/current")).toStdWString(),
		registry);

	const QString stableRoot = QDir::toNativeSeparators(
		tempRoot.path() + QStringLiteral("/EqualizerAPO-XT/config"));
	expectEqual(QString::fromStdWString(registry.readValue(APP_REGPATH, L"ConfigPath")),
		stableRoot,
		QStringLiteral("the volatile tree's ConfigPath is repointed at the stable root"));
	expectTrue(QFile::exists(stableRoot + QStringLiteral("/config.txt")),
		QStringLiteral("the volatile tree's files are rescued into the stable root"));
	expectTrue(registry.valueExists(APP_REGPATH, L"MigratedFrom"),
		QStringLiteral("a rescue leaves the MigratedFrom breadcrumb"));
	expectTrue(registry.valueExists(APP_REGPATH, L"MigrationStamp"),
		QStringLiteral("a rescue leaves the MigrationStamp breadcrumb"));
	expectTrue(registry.valueExists(APP_REGPATH, L"MigratedFiles"),
		QStringLiteral("a rescue records how many files moved"));
}
