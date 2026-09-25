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
#include "Editor/import/CallerProfileCheck.h"
#include "services/security/AudioEngineAccess.h"
#include "services/security/ConfigDirectoryHandles.h"

#include <QProcess>
#include <aclapi.h>
#include <shellapi.h>
#include "platform/windows/CommandLineQuoting.h"
#include <cstdio>
#include <map>

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

class ProfileTable : public EqAPO::Import::CallerProfileCheck::FileSystem
{
public:
	std::wstring root = L"C:\\Users";
	bool fixed = true;
	std::map<std::wstring, bool> directories;

	void addChain(const std::wstring& path)
	{
		for (const auto& part : configaccess::directoryChain(configaccess::fullLocalPath(path)))
			directories[part] = true;
	}
	std::wstring profilesRoot() const override { return root; }
	bool isFixedDrive(const std::wstring&) const override { return fixed; }
	bool isPlainDirectory(const std::wstring& path, std::wstring& reason) const override
	{
		for (const auto& [name, plain] : directories)
		{
			if (configaccess::samePath(name, path))
			{
				if (!plain)
					reason = L"reparse point or non-directory: " + path;
				return plain;
			}
		}
		reason = L"missing component: " + path;
		return false;
	}
};

QByteArray daclBytes(const QString& path)
{
	winutil::UniqueLocalPtr<void> descriptor;
	PACL acl = nullptr;
	const auto native = QDir::toNativeSeparators(path).toStdWString();
	requireTrue(GetNamedSecurityInfoW(native.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &acl, nullptr, descriptor.put()) == ERROR_SUCCESS && acl != nullptr,
		QStringLiteral("test DACL read"));
	return QByteArray(reinterpret_cast<const char*>(acl), acl->AclSize);
}

class ScratchJunction
{
public:
	ScratchJunction(const QString& link, const QString& target) : path(QDir::toNativeSeparators(link).toStdWString())
	{
		const QString command = QStringLiteral("mklink /J \"%1\" \"%2\"").arg(
			QDir::toNativeSeparators(link), QDir::toNativeSeparators(target));
		QProcess process;
		process.setProgram(qEnvironmentVariable("COMSPEC"));
		// cmd.exe does not use CommandLineToArgvW's backslash quoting.
		process.setNativeArguments(QStringLiteral("/d /c ") + command);
		process.start();
		requireTrue(process.waitForFinished() && process.exitCode() == 0,
			QStringLiteral("scratch junction created without elevation"));
	}
	~ScratchJunction() { RemoveDirectoryW(path.c_str()); }
	ScratchJunction(const ScratchJunction&) = delete;
	ScratchJunction& operator=(const ScratchJunction&) = delete;
private:
	std::wstring path;
};
}

void testCallerProfileCheckRejectsUnsafePaths()
{
	using EqAPO::Import::CallerProfileCheck::verify;
	ProfileTable table;
	const std::wstring valid = L"C:\\Users\\me\\AppData\\Local";
	table.addChain(valid);
	std::wstring reason;
	expectTrue(verify(valid, reason, table).has_value(), QStringLiteral("plain profile accepted"));
	expectTrue(verify(L"c:/USERS/me/AppData/Local", reason, table).has_value(),
		QStringLiteral("case and separator comparison follows normalized Windows paths"));
	const std::wstring refused[] = {
		L"\\\\server\\share\\me\\AppData\\Local", L"\\\\?\\C:\\Users\\me\\AppData\\Local",
		L"\\\\.\\C:\\Users\\me\\AppData\\Local", L"\\??\\C:\\Users\\me\\AppData\\Local",
		L"C:\\Elsewhere\\me\\AppData\\Local", L"C:\\UsersExtra\\me\\AppData\\Local",
		L"C:\\Users\\nested\\me\\AppData\\Local", L"C:Users\\me\\AppData\\Local",
		L"C:\\Users\\me\\AppData\\Local\\..\\Roaming", L"C:\\Users\\..\\Windows\\me\\AppData\\Local",
		L"C:\\Users\\me\\AppData\\Local:stream", L"C:\\Users\\me\\AppData\\Local\\child"};
	for (const auto& path : refused)
	{
		expectFalse(verify(path, reason, table).has_value(), QStringLiteral("unsafe spelling refused"));
		expectFalse(reason.empty(), QStringLiteral("rejection has a reason"));
	}
	expectTrue(verify(L"C:\\Users\\me\\AppData\\Local\\..\\Local", reason, table).has_value(),
		QStringLiteral("normalization that stays in the allowed folder is accepted"));
	for (auto& [component, plain] : table.directories)
	{
		plain = false;
		expectFalse(verify(valid, reason, table).has_value(), QStringLiteral("reparse at every component refused"));
		plain = true;
	}
	table.directories.erase(L"C:\\Users\\me\\AppData");
	expectFalse(verify(valid, reason, table).has_value(), QStringLiteral("missing ancestor refused"));
	table.addChain(valid);
	table.fixed = false;
	expectFalse(verify(valid, reason, table).has_value(), QStringLiteral("removable drive refused"));
	std::puts("CallerProfileCheck: normalized path table passed");
}

void testLegacyMigrationHookUsesVerifiedCallerOrFallsBack()
{
	using Migration = EqAPO::Import::LegacyMigration;
	QTemporaryDir temp;
	requireTrue(temp.isValid(), QStringLiteral("hook profile fixture created"));
	const QString caller = temp.path() + QStringLiteral("/profiles/me/AppData/Local");
	requireTrue(QDir().mkpath(caller), QStringLiteral("profile folders created"));
	ScopedLocalAppData environment(caller);
	ProfileTable table;
	table.root = QDir::toNativeSeparators(temp.path() + QStringLiteral("/profiles")).toStdWString();
	const auto nativeCaller = QDir::toNativeSeparators(caller).toStdWString();
	table.addChain(nativeCaller);
	test::FakeRegistry registry;
	registry.seedKey(APP_REGPATH);
	const QString shipped = temp.path() + QStringLiteral("/config");
	requireTrue(QDir().mkpath(shipped), QStringLiteral("sample directory created"));
	{
		QFile file(shipped + QStringLiteral("/config.txt"));
		requireTrue(file.open(QIODevice::WriteOnly), QStringLiteral("sample created"));
		file.write("Preamp: -2 dB\n");
	}
	const auto outcome = Migration::prepareHookStep(temp.path().toStdWString(), registry);
	if (AudioEngineAccess::isElevated())
	{
		expectTrue(outcome == L"failed", QStringLiteral("prepare rejects elevated token"));
		std::puts("SKIP: unelevated prepare assertions (process elevated)");
		return;
	}
	expectTrue(outcome == L"adopt", QStringLiteral("unelevated prepare succeeds"));
	// Keep the install fixture separate from the stable profile fixture, as in
	// a real Velopack current directory. Custom ConfigPath still needs grants.
	const QString install = temp.path() + QStringLiteral("/package/current");
	requireTrue(QDir().mkpath(install + QStringLiteral("/config")), QStringLiteral("install-shaped folder created"));
	{
		QFile dll(install + QStringLiteral("/fixture.dll"));
		requireTrue(dll.open(QIODevice::WriteOnly), QStringLiteral("pre-existing install file created"));
		dll.write("fixture");
	}
	test::FakeRegistry customInstall;
	customInstall.seedKey(APP_REGPATH);
	customInstall.seedString(APP_REGPATH, L"ConfigPath", L"D:\\CustomConfig");
	Migration::Handoff installDetails;
	expectTrue(Migration::prepareHookStep(install.toStdWString(), customInstall, &installDetails) == L"respect-custom",
		QStringLiteral("install grants run even when migration respects custom config"));
	expectTrue(installDetails.installGrantsPrepared, QStringLiteral("both owned install grants reported complete"));
	const unsigned long rx = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
	const unsigned long modify = rx | FILE_GENERIC_WRITE | DELETE;
	for (const auto& path : {install, install + QStringLiteral("/fixture.dll"), install + QStringLiteral("/config")})
	{
		const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
		const unsigned long expectedAccess = path.endsWith(QStringLiteral("/config")) ? modify : rx;
		expectTrue((AudioEngineAccess::accessForUsers(nativePath) & expectedAccess) == expectedAccess,
			QStringLiteral("Users install RX and packaged config Modify"));
		expectTrue((AudioEngineAccess::accessForAudioEngine(nativePath) & expectedAccess) == expectedAccess,
			QStringLiteral("LOCAL SERVICE install RX and packaged config Modify"));
		if (expectedAccess == rx)
			expectTrue((AudioEngineAccess::accessForAudioEngine(nativePath) & (FILE_WRITE_DATA | DELETE | WRITE_DAC)) == 0,
				QStringLiteral("engine install grant does not add write or ACL control"));
	}
	for (const auto& value : {L"0", L"true", L"", L"bad"})
		expectFalse(Migration::parseHandoff({L"--caller-install-grants-prepared", value}).installGrantsPrepared,
			QStringLiteral("only literal one acknowledges both install grants"));
	expectTrue(Migration::parseHandoff({L"--caller-install-grants-prepared", L"1"}).installGrantsPrepared,
		QStringLiteral("prepared install flag parses"));
	expectFalse(Migration::parseHandoff({L"--caller-install-grants-prepared"}).installGrantsPrepared,
		QStringLiteral("missing flag value keeps legacy behavior"));
	expectFalse(Migration::parseHandoff({}).installGrantsPrepared, QStringLiteral("absent flag keeps legacy behavior"));
	expectFalse(Migration::parseHandoff({L"--caller-install-grants-prepared", L"1",
		L"--caller-install-grants-prepared", L"0"}).installGrantsPrepared,
		QStringLiteral("last prepared flag wins"));
	std::puts("Install preparation: unelevated RX/Modify and hand-off flag passed");
	expectFalse(registry.valueExists(APP_REGPATH, L"ConfigPath"), QStringLiteral("prepare never writes registry"));
	const QString root = caller + QStringLiteral("/EqualizerAPO-XT/config");
	expectTrue(QFile::exists(root + QStringLiteral("/config.txt")), QStringLiteral("prepare seeds samples"));
	const auto before = daclBytes(root);
	requireTrue(Migration::recordPreparedHookStep({nativeCaller, outcome}, registry, &table),
		QStringLiteral("valid prepared profile handled"));
	expectEqual(QString::fromStdWString(registry.readValue(APP_REGPATH, L"ConfigPath")),
		QDir::toNativeSeparators(root), QStringLiteral("record writes caller root"));
	expectTrue(before == daclBytes(root), QStringLiteral("record does not change ACL"));
	expectFalse(registry.valueExists(APP_REGPATH, L"MigratedFrom"), QStringLiteral("record only writes ConfigPath"));

	test::FakeRegistry invalid;
	invalid.seedKey(APP_REGPATH);
	expectFalse(Migration::recordPreparedHookStep({L"\\\\server\\share", L"adopt"}, invalid, &table),
		QStringLiteral("invalid caller refused without file work"));
	expectFalse(invalid.valueExists(APP_REGPATH, L"ConfigPath"), QStringLiteral("invalid caller cannot write ConfigPath"));
	for (const auto& hint : {L"adopt", L"garbage", L"respect-custom", L"failed"})
	{
		invalid.seedString(APP_REGPATH, L"ConfigPath", L"D:\\Custom");
		Migration::recordPreparedHookStep({nativeCaller, hint}, invalid, &table);
		expectTrue(invalid.readValue(APP_REGPATH, L"ConfigPath") == L"D:\\Custom",
			QStringLiteral("untrusted hint cannot override custom path"));
	}
	const auto parsed = Migration::parseHandoff({L"--veloapp-install", L"--caller-localappdata", L"bad",
		L"--caller-localappdata", nativeCaller, L"--caller-migration-outcome", L"adopt"});
	expectTrue(parsed.localAppData == nativeCaller && parsed.outcome == L"adopt",
		QStringLiteral("last hand-off values win"));
	const std::wstring unicodePath = L"C:\\Users\\\uD55C \uAE00\\AppData\\Local";
	const std::wstring command = L"Editor.exe " + winutil::joinCommandLineArguments(
		{L"--caller-localappdata", unicodePath, L"--caller-migration-outcome", L"volatile"});
	int argc = 0;
	winutil::UniqueLocalPtr<wchar_t*> argv(CommandLineToArgvW(command.c_str(), &argc));
	requireTrue(bool(argv), QStringLiteral("wide hand-off command parses"));
	std::vector<std::wstring> arguments;
	for (int i = 1; i < argc; ++i)
		arguments.emplace_back(argv.get()[i]);
	const auto roundTrip = Migration::parseHandoff(arguments);
	expectTrue(roundTrip.localAppData == unicodePath && roundTrip.outcome == L"volatile",
		QStringLiteral("Unicode hand-off survives quoting and Windows argument parsing"));
	expectFalse(Migration::parseHandoff({}).localAppData.has_value(), QStringLiteral("missing hand-off stays absent"));
	expectTrue(Migration::parseHandoff({L"--caller-localappdata"}).localAppData == L"",
		QStringLiteral("missing path value is invalid, not an out-of-bounds access"));
	// A removed prepared root must not be recreated by record.
	test::FakeRegistry missing;
	missing.seedKey(APP_REGPATH);
	const QString otherCaller = temp.path() + QStringLiteral("/profiles/other/AppData/Local");
	requireTrue(QDir().mkpath(otherCaller), QStringLiteral("unprepared profile exists"));
	const auto nativeOther = QDir::toNativeSeparators(otherCaller).toStdWString();
	table.addChain(nativeOther);
	Migration::recordPreparedHookStep({nativeOther, L"adopt"}, missing, &table);
	expectFalse(missing.valueExists(APP_REGPATH, L"ConfigPath"), QStringLiteral("missing prepared root is not recorded"));
	expectFalse(QDir(otherCaller + QStringLiteral("/EqualizerAPO-XT")).exists(), QStringLiteral("record creates no directory"));
	{
		ScratchJunction junction(otherCaller + QStringLiteral("/EqualizerAPO-XT"), root);
		const auto rootAcl = daclBytes(root);
		Migration::recordPreparedHookStep({nativeOther, L"adopt"}, missing, &table);
		expectFalse(missing.valueExists(APP_REGPATH, L"ConfigPath"), QStringLiteral("record refuses a junction in prepared root"));
		expectTrue(rootAcl == daclBytes(root), QStringLiteral("record leaves junction target ACL alone"));
	}

	const QString volatileRoot = temp.path() + QStringLiteral("/EqualizerAPO-XT-test/current/config");
	requireTrue(QDir().mkpath(volatileRoot), QStringLiteral("volatile source created"));
	{
		QFile file(volatileRoot + QStringLiteral("/rescued.txt"));
		requireTrue(file.open(QIODevice::WriteOnly), QStringLiteral("volatile source file created"));
		file.write("Preamp: -4 dB\n");
	}
	registry.seedString(APP_REGPATH, L"ConfigPath", QDir::toNativeSeparators(volatileRoot).toStdWString());
	Migration::Handoff prepared;
	prepared.localAppData = nativeCaller;
	const auto preparedOutcome = Migration::prepareHookStep(temp.path().toStdWString(), registry, &prepared);
	prepared.localAppData = nativeCaller;
	prepared.outcome = preparedOutcome;
	expectTrue(preparedOutcome == L"volatile", QStringLiteral("prepare rescues volatile source unelevated"));
	expectTrue(QFile::exists(root + QStringLiteral("/rescued.txt")), QStringLiteral("volatile file copied"));
	expectTrue(registry.readValue(APP_REGPATH, L"ConfigPath") == QDir::toNativeSeparators(volatileRoot).toStdWString(),
		QStringLiteral("copy does not write HKLM"));
	const auto parsedDetails = Migration::parseHandoff({L"--caller-localappdata", nativeCaller,
		L"--caller-migration-outcome", prepared.outcome, L"--caller-migrated-from", prepared.migratedFrom,
		L"--caller-migrated-files", prepared.migratedFiles});
	Migration::recordPreparedHookStep(parsedDetails, registry, &table);
	test::FakeRegistry fallbackRegistry;
	fallbackRegistry.seedKey(APP_REGPATH);
	fallbackRegistry.seedString(APP_REGPATH, L"ConfigPath", QDir::toNativeSeparators(volatileRoot).toStdWString());
	{
		const QString fallbackRoot = temp.path() + QStringLiteral("/fallback");
		ScopedLocalAppData fallbackEnvironment(fallbackRoot);
		Migration::runElevatedHookStep(temp.path().toStdWString(), fallbackRegistry);
	}
	for (const auto* key : {L"MigratedFrom", L"MigrationStamp", L"MigratedFiles"})
	{
		expectTrue(registry.valueExists(APP_REGPATH, key) && fallbackRegistry.valueExists(APP_REGPATH, key),
			QStringLiteral("accepted-caller and fallback write the same breadcrumb keys"));
	}
	expectTrue(registry.readValue(APP_REGPATH, L"MigratedFrom") == fallbackRegistry.readValue(APP_REGPATH, L"MigratedFrom"),
		QStringLiteral("migration source matches fallback"));
	expectTrue(registry.readDWORDValue(APP_REGPATH, L"MigratedFiles") == fallbackRegistry.readDWORDValue(APP_REGPATH, L"MigratedFiles"),
		QStringLiteral("copied-file count matches fallback"));
	expectFalse(registry.readValue(APP_REGPATH, L"MigrationStamp").empty(), QStringLiteral("startup notice has a stamp"));
	for (const auto& bad : {std::wstring(4097, L'x'), std::wstring(L"source\ncontrol"), std::wstring(L"source\x7f")})
	{
		test::FakeRegistry rejected;
		rejected.seedKey(APP_REGPATH);
		rejected.seedString(APP_REGPATH, L"ConfigPath", prepared.migratedFrom);
		auto malformed = prepared;
		malformed.migratedFrom = bad;
		Migration::recordPreparedHookStep(malformed, rejected, &table);
		expectFalse(rejected.valueExists(APP_REGPATH, L"MigratedFrom"), QStringLiteral("invalid display metadata refused"));
		expectTrue(rejected.readValue(APP_REGPATH, L"ConfigPath") == prepared.migratedFrom,
			QStringLiteral("invalid display metadata writes no registry values"));
	}
	for (const auto& count : {L"2147483648", L"-1", L"1\t", L"00000000000"})
	{
		test::FakeRegistry rejected;
		rejected.seedKey(APP_REGPATH);
		rejected.seedString(APP_REGPATH, L"ConfigPath", prepared.migratedFrom);
		auto malformed = prepared;
		malformed.migratedFiles = count;
		Migration::recordPreparedHookStep(malformed, rejected, &table);
		expectFalse(rejected.valueExists(APP_REGPATH, L"MigrationStamp"), QStringLiteral("invalid count refused"));
	}
	std::puts("LegacyMigration: prepare, record, hand-off and validated breadcrumbs passed");
}

void testConfigHandleGrantRejectsJunctionsAndPreservesChildren()
{
	using AudioEngineAccess::Grant;
	QTemporaryDir temp;
	requireTrue(temp.isValid(), QStringLiteral("handle grant fixture created"));
	const QString target = temp.path() + QStringLiteral("/outside");
	const QString local = temp.path() + QStringLiteral("/local");
	requireTrue(QDir().mkpath(target) && QDir().mkpath(local), QStringLiteral("junction fixture targets created"));
	const QByteArray outsideBefore = daclBytes(target);
	{
		ScratchJunction junction(local + QStringLiteral("/EqualizerAPO-XT"), target);
		const auto candidate = QDir::toNativeSeparators(local + QStringLiteral("/EqualizerAPO-XT")).toStdWString();
		expectTrue(AudioEngineAccess::grantOwnedConfigAccess(candidate) == Grant::Failed,
			QStringLiteral("grant refuses final reparse object"));
		std::wstring reason;
		expectFalse(EqAPO::Import::CallerProfileCheck::verifyPreparedRoot(candidate, reason),
			QStringLiteral("record read-only check refuses junction"));
	}
	const QString config = local + QStringLiteral("/EqualizerAPO-XT/config");
	requireTrue(QDir().mkpath(config + QStringLiteral("/existing")), QStringLiteral("pre-existing config created"));
	{
		QFile file(config + QStringLiteral("/old.txt"));
		requireTrue(file.open(QIODevice::WriteOnly), QStringLiteral("pre-existing file created"));
		file.write("old\n");
	}
	const auto native = QDir::toNativeSeparators(config).toStdWString();
	// Existing unprotected children receive normal inheritance propagation.
	ScratchJunction child(config + QStringLiteral("/link"), target);
	const Grant result = AudioEngineAccess::grantOwnedConfigAccess(native);
	if (!AudioEngineAccess::isElevated())
	{
		expectTrue(result == Grant::Applied, QStringLiteral("unelevated owned-folder grant applied"));
		expectTrue(AudioEngineAccess::isReadableByAudioEngine(
			QDir::toNativeSeparators(config + QStringLiteral("/old.txt")).toStdWString()),
			QStringLiteral("old file receives inheritable audio access"));
		constexpr unsigned long modify = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;
		for (const auto& path : {config, config + QStringLiteral("/new")})
		{
			requireTrue(QDir().mkpath(path), QStringLiteral("new directory inherits grant"));
			const auto name = QDir::toNativeSeparators(path).toStdWString();
			expectTrue((AudioEngineAccess::accessForUsers(name) & modify) == modify,
				QStringLiteral("Users Modify on root and new directory"));
			expectTrue((AudioEngineAccess::accessForAudioEngine(name) & modify) == modify,
				QStringLiteral("LOCAL SERVICE Modify on root and new directory"));
		}
		std::puts("Config owned grant: unelevated ACL and inheritance assertions passed");
	}
	else
	{
		expectTrue(result == Grant::Failed, QStringLiteral("owned-folder grant refuses elevated token"));
		std::puts("SKIP: owned-folder grant assertions (process elevated)");
	}
	expectTrue(daclBytes(target) == outsideBefore, QStringLiteral("junction target DACL unchanged"));
	std::puts("Config owned grant: junction target unchanged");
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
