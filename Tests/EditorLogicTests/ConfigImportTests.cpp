/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	Config import and migration: config-relative convolution path
	resolution, the dependency scanner and import executor, and the
	legacy-install migration policy the elevated hook acts on.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "text/WideString.h"
#include "platform/windows/TextEncoding.h"

#include <string>

#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QScopeGuard>

#include "filters/ConfigFileReference.h"
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include "Editor/ConfigFileCodec.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportExecutor.h"
#include "Editor/import/ImportManifest.h"
#include "Editor/import/LegacyMigrationPolicy.h"
#include "Editor/widgets/cards/FileReferenceController.h"

#include "../TestHarness.h"
#include "EditorLogicTestSupport.h"

namespace
{
QString bundleModuleRelative()
{
#if defined(_M_ARM64)
	return QStringLiteral("Contents/arm64-win/Foo.vst3");
#elif defined(_WIN64)
	return QStringLiteral("Contents/x86_64-win/Foo.vst3");
#else
	return QStringLiteral("Contents/x86-win/Foo.vst3");
#endif
}

void writeBundleFile(const QString& path, const QByteArray& bytes)
{
	requireTrue(QDir().mkpath(QFileInfo(path).absolutePath()), "create bundle file directory");
	QFile file(path);
	requireTrue(file.open(QIODevice::WriteOnly), "open bundle fixture file");
	requireTrue(file.write(bytes) == bytes.size(), "write bundle fixture bytes");
}

QString makePluginBundle(const QString& root)
{
	const QString bundle = root + QStringLiteral("/VST3/Foo.vst3");
	writeBundleFile(bundle + "/" + bundleModuleRelative(), QByteArray(128, 'm'));
	writeBundleFile(bundle + "/Contents/Resources/moduleinfo.json", QByteArray("{}"));
	writeBundleFile(bundle + "/note.json", QByteArray("{\"note\":1}"));
	return bundle;
}

void expectManifestEqual(const EqAPO::Import::ImportManifest& actual,
	const EqAPO::Import::ImportManifest& expected)
{
	expectEqual(actual.rootSource, expected.rootSource, "same rootSource");
	expectEqual(actual.rootSourceDir, expected.rootSourceDir, "same rootSourceDir");
	expectEqual(actual.rootDest, expected.rootDest, "same rootDest");
	expectEqual(actual.externalReferences, expected.externalReferences, "same externalReferences");
	expectEqual(actual.warnings, expected.warnings, "same warnings");
	expectTrue(actual.totalBytes == expected.totalBytes, "same totalBytes");
	expectTrue(actual.hasErrors == expected.hasErrors, "same hasErrors");
	requireEqual(int(actual.items.size()), int(expected.items.size()), "same item count");
	for (qsizetype i = 0; i < actual.items.size(); ++i)
	{
		const auto& left = actual.items[i];
		const auto& right = expected.items[i];
		expectEqual(left.sourceAbsolute, right.sourceAbsolute, "same sourceAbsolute");
		expectEqual(left.destRelative, right.destRelative, "same destRelative");
		expectTrue(left.sizeBytes == right.sizeBytes, "same sizeBytes");
		expectTrue(left.exists == right.exists, "same exists");
		expectEqual(left.kind, right.kind, "same kind");
	}
}

void expectBundleFiles(const EqAPO::Import::ImportManifest& manifest, const QString& bundle)
{
	expectFalse(manifest.hasErrors, "bundle has no scan errors");
	expectTrue(manifest.externalReferences.isEmpty(), "explicit import has no external references");
	requireEqual(int(manifest.items.size()), 3, "bundle has three plain files");
	expectPath(manifest.rootSourceDir, QFileInfo(bundle).absolutePath());
	expectTrue(manifest.totalBytes == 140, "bundle total includes module, resources and metadata");
	QStringList destinations;
	for (const auto& item : manifest.items)
	{
		destinations.append(item.destRelative);
		expectEqual(item.kind, "VSTPlugin", "every bundle item is a plugin file");
		expectTrue(item.exists, "every bundle file exists");
		expectPath(item.sourceAbsolute, QDir(manifest.rootSourceDir).filePath(
			item.destRelative.mid(QStringLiteral("VST3/").size())));
		expectTrue(item.sizeBytes == QFileInfo(item.sourceAbsolute).size(), "each item has its file size");
	}
	expectTrue(destinations.contains("VST3/Foo.vst3/" + bundleModuleRelative()), "module keeps platform folder");
	expectTrue(destinations.contains("VST3/Foo.vst3/Contents/Resources/moduleinfo.json"), "resources keep their folder");
	expectTrue(destinations.contains("VST3/Foo.vst3/note.json"), "root metadata is included");
}

void expectImportedBundleOpens(const EqAPO::Import::ImportManifest& manifest, const QString& configDir)
{
	const auto result = EqAPO::Import::ImportExecutor::execute(manifest, configDir);
	expectTrue(result.success, "bundle copy succeeds");
	expectEqual(result.filesCopied, 3, "copies every bundle file");
	expectTrue(result.bytesCopied == manifest.totalBytes, "copies the expected number of bytes");
	const auto target = ConfigFileReference::library(L"",
		QDir::toNativeSeparators(configDir + "/" + manifest.rootDest).toStdWString(),
		QDir::toNativeSeparators(configDir + "/config.txt").toStdWString());
	requireTrue(bool(target), "the engine opens the imported library reference");
	const QString module = QString::fromStdWString(target.path.path());
	expectPath(module, configDir + "/VST3/Foo.vst3/" + bundleModuleRelative());
	expectFalse(QDir(configDir).relativeFilePath(module).startsWith(".."), "opened module stays below config");
}

bool makeBundleLink(const QString& link, const QString& target, bool junction)
{
	QProcess process;
	process.setProgram(qEnvironmentVariable("COMSPEC", "C:/Windows/System32/cmd.exe"));
	process.setNativeArguments(QStringLiteral("/d /c mklink %1\"%2\" \"%3\"")
		.arg(junction ? QStringLiteral("/J ") : QString(),
			QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)));
	process.start();
	const bool finished = process.waitForFinished(10000);
	if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
	{
		qInfo().noquote() << (junction ? "SKIP bundle junction:" : "SKIP bundle file symlink:")
			<< process.readAllStandardError();
		return false;
	}
	return true;
}
}

TEST_CASE(EditorLogicTests, testPluginBundleFolderImport)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary bundle directory must be valid");
	const QString bundle = makePluginBundle(temp.path());
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(
		bundle, temp.path() + "/config/config.txt");
	expectFalse(manifest.hasErrors,
		QStringLiteral("bundle folder must import instead of Missing file: %1").arg(manifest.warnings.join('\n')));
	requireEqual(int(manifest.items.size()), 3, "bundle import includes module, resources and root metadata");
	expectBundleFiles(manifest, bundle);
	expectTrue(manifest.warnings.isEmpty(), "plain bundle has no warnings");
	expectPath(manifest.rootSource, bundle);
	expectEqual(manifest.rootDest, "VST3/Foo.vst3", "folder reference still names the folder");
	expectImportedBundleOpens(manifest, temp.path() + "/config");
}

TEST_CASE(EditorLogicTests, testPluginBundleModuleImport)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary bundle directory must be valid");
	const QString bundle = makePluginBundle(temp.path());
	const QString module = bundle + "/" + bundleModuleRelative();
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(
		module, temp.path() + "/config/config.txt");
	expectBundleFiles(manifest, bundle);
	expectTrue(manifest.warnings.isEmpty(), "plain module has no warnings");
	expectPath(manifest.rootSource, module);
	expectEqual(manifest.rootDest, "VST3/Foo.vst3/" + bundleModuleRelative(), "module reference still names the module");
	expectImportedBundleOpens(manifest, temp.path() + "/config");
}

TEST_CASE(EditorLogicTests, testPluginBundleSkipsJunction)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary bundle directory must be valid");
	const QString bundle = makePluginBundle(temp.path());
	const QString outside = temp.path() + "/private";
	writeBundleFile(outside + "/secret.txt", QByteArray("private-content"));
	const QString link = bundle + "/linked";
	if (!makeBundleLink(link, outside, true))
		return;
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(
		bundle, temp.path() + "/config/config.txt");
	expectBundleFiles(manifest, bundle);
	expectTrue(manifest.warnings.contains("Link not copied: " + link), "junction is reported, not followed");
	expectImportedBundleOpens(manifest, temp.path() + "/config");
	expectFalse(QFileInfo::exists(temp.path() + "/config/VST3/Foo.vst3/linked/secret.txt"), "junction target is never copied");
	expectFalse(QFileInfo::exists(temp.path() + "/config/VST3/Foo.vst3/linked"), "junction itself is never copied");
	requireTrue(QDir().rmdir(link), "remove only the test junction");
	expectTrue(QFile::exists(outside + "/secret.txt"), "original junction target is untouched");
	qInfo("Bundle junction: skipped with warning, no target content copied");
}

TEST_CASE(EditorLogicTests, testPluginBundleSkipsFileSymlink)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary bundle directory must be valid");
	const QString bundle = makePluginBundle(temp.path());
	const QString outside = temp.path() + "/private/secret.txt";
	writeBundleFile(outside, QByteArray("private-content"));
	const QString link = bundle + "/secret.txt";
	if (!makeBundleLink(link, outside, false))
		return;
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(
		bundle, temp.path() + "/config/config.txt");
	expectBundleFiles(manifest, bundle);
	expectTrue(manifest.warnings.contains("Link not copied: " + link), "file symlink is reported, not followed");
	expectImportedBundleOpens(manifest, temp.path() + "/config");
	expectFalse(QFileInfo::exists(temp.path() + "/config/VST3/Foo.vst3/secret.txt"), "file link target is never copied");
	requireTrue(QFile::remove(link), "remove only the test file symlink");
	expectTrue(QFile::exists(outside), "original file link target is untouched");
	qInfo("Bundle file symlink: skipped with warning, no target content copied");
}

TEST_CASE(EditorLogicTests, testPluginSingleFileMatchesConfigScan)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary plugin directory must be valid");
	const QString config = temp.path() + "/config/config.txt";
	for (const QString& suffix : {QStringLiteral("dll"), QStringLiteral("vst3")})
	{
		const QString library = temp.path() + "/VST3/Foo." + suffix;
		writeBundleFile(library, QByteArray("single-module"));
		expectManifestEqual(EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(library, config),
			EqAPO::Import::ConfigDependencyScanner::scan(library, QFileInfo(config).absolutePath()));
	}
}

TEST_CASE(EditorLogicTests, testConfigScanKeepsPluginBundleExternal)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary bundle directory must be valid");
	const QString bundle = makePluginBundle(temp.path());
	const QString config = temp.path() + "/source/main.txt";
	writeBundleFile(config, QStringLiteral("VSTPlugin: Library \"%1\"\n").arg(bundle).toUtf8());
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scan(config, temp.path() + "/config");
	expectFalse(manifest.hasErrors, "external bundle does not block config import");
	requireEqual(int(manifest.items.size()), 1, "only config is copied, not the external bundle");
	expectPath(manifest.items[0].sourceAbsolute, config);
	expectEqual(manifest.externalReferences, QStringList{bundle}, "config scan records the bundle reference");
}

TEST_CASE(EditorLogicTests, testPluginBundleAlreadyInPlace)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary bundle directory must be valid");
	const QString bundle = makePluginBundle(temp.path());
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(
		bundle, temp.path() + "/config.txt");
	const auto result = EqAPO::Import::ImportExecutor::execute(manifest, temp.path());
	expectTrue(result.success, "in-place bundle import succeeds");
	expectEqual(result.filesCopied, 0, "in-place bundle copies nothing");
	expectEqual(result.filesAlreadyInPlace, 3, "every bundle file is already in place");
	expectTrue(result.bytesCopied == 0, "no bytes copied in place");
	QStringList destinations;
	for (const auto& item : manifest.items)
		destinations.append(QDir(temp.path()).absoluteFilePath(item.destRelative));
	expectEqual(result.alreadyInPlace, destinations, "per-file grants receive every in-place destination");
}

TEST_CASE(EditorLogicTests, testPluginFolderWithoutModuleStillReportsMissingFile)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary plugin directory must be valid");
	for (const QString& name : {QStringLiteral("ordinary"), QStringLiteral("Empty.vst3")})
	{
		const QString folder = temp.path() + "/" + name;
		requireTrue(QDir().mkpath(folder), "create non-bundle folder");
		const auto manifest = EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(folder, temp.path() + "/config.txt");
		expectTrue(manifest.hasErrors, "folder without platform module remains an error");
		expectTrue(manifest.warnings.contains("Missing file: " + folder), "keeps existing missing-file warning");
		expectManifestEqual(manifest, EqAPO::Import::ConfigDependencyScanner::scan(folder, temp.path()));
	}
}

TEST_CASE(EditorLogicTests, testPluginBundleUnreadableResourcesBlockImportButKeepModuleItem)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "temporary bundle directory must be valid");
	const QString bundle = makePluginBundle(temp.path());
	const QString resources = bundle + "/Contents/Resources";
	const auto changeAccess = [&](const QString& action, const QString& principal) {
		QProcess process;
		process.setProgram(qEnvironmentVariable("SystemRoot", "C:/Windows") + "/System32/icacls.exe");
		process.setArguments({QDir::toNativeSeparators(resources), action, principal});
		process.start();
		if (!process.waitForFinished(10000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
	};
	// Register cleanup before applying the deny, including the skip and failed
	// assertion paths. The deny is not inherited by the resource file.
	auto restoreAccess = qScopeGuard([&] {
		if (!changeAccess(QStringLiteral("/remove:d"), QStringLiteral("*S-1-1-0")))
			qFatal("Bundle unreadable resources: could not remove the test deny entry");
		qInfo("Bundle unreadable resources: test deny entry removed");
	});
	if (!changeAccess(QStringLiteral("/deny"), QStringLiteral("*S-1-1-0:(RD)")))
	{
		qInfo("SKIP bundle unreadable resources: could not deny directory listing");
		return;
	}
	qInfo("Bundle unreadable resources: directory listing deny applied");
	expectTrue(QDir(resources).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
		"Qt returns an empty listing for the denied resource folder");
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(
		bundle, temp.path() + "/config/config.txt");
	// Keep scanning readable siblings for the dialog, but forbid an incomplete import.
	bool moduleListed = false;
	for (const auto& item : manifest.items)
		moduleListed = moduleListed || item.sourceAbsolute == bundle + "/" + bundleModuleRelative();
	qInfo().noquote() << "Bundle unreadable resources: hasErrors=" << manifest.hasErrors
		<< "moduleListed=" << moduleListed << "warnings=" << manifest.warnings.join('\n');
	expectTrue(manifest.hasErrors, "an unreadable bundle resource folder must block import");
	expectTrue(manifest.warnings.contains("Cannot open " + resources + " for scanning."),
		"an unreadable bundle resource folder must have a warning naming the folder");
	expectTrue(moduleListed, "readable module remains listed despite unreadable resources");
	restoreAccess.commit();
	expectBundleFiles(EqAPO::Import::ConfigDependencyScanner::scanPluginLibrary(
		bundle, temp.path() + "/config/config.txt"), bundle);
}

TEST_CASE(EditorLogicTests, testLegacyConvolutionPathRules)
{
	const QString configPath = "C:/EqualizerAPO/config/config.txt";
	qputenv("EAPO_XT_TEST_IR_DIR", "C:\\Impulse Responses");

	expectPath(
		FileReferenceController::absolutePathForConfig(configPath, "irs/room.wav"),
		"C:/EqualizerAPO/config/irs/room.wav");
	expectPath(
		FileReferenceController::absolutePathForConfig(configPath, "C:/Impulse/room.wav"),
		"C:/Impulse/room.wav");
	expectTrue(
		FileReferenceController::absolutePathForConfig(configPath, QString()).isEmpty(),
		"empty convolution path should remain empty");
	expectPath(
		FileReferenceController::absolutePathForConfig(
			configPath, "\"%EAPO_XT_TEST_IR_DIR%\\quoted room.wav\""),
		"C:/Impulse Responses/quoted room.wav");

	expectPath(
		FileReferenceController::displayPathForConfigSelection(configPath, "C:/EqualizerAPO/config/irs/room.wav"),
		"irs/room.wav");
	// One rule with the cards (maintainer decision 2026-09-25): one level
	// above the config folder is still written relative.
	expectPath(
		FileReferenceController::displayPathForConfigSelection(configPath, "C:/EqualizerAPO/shared/room.wav"),
		"../shared/room.wav");
	expectPath(
		FileReferenceController::displayPathForConfigSelection(configPath, "C:/Other/deep/room.wav"),
		"C:/Other/deep/room.wav");
	expectPath(
		FileReferenceController::displayPathForConfigSelection(configPath, "C:/Impulse/room.wav"),
		"C:/Impulse/room.wav");

}

// Audit #348 A1: the import scanner and the card read a line's file the way
// the engine does (ConfigFileReference), so a quoted Include or one through a
// variable names the same file in all three; and the card's verdict applies
// the engine's location rule before the ACL.
TEST_CASE(EditorLogicTests, testFileReferencesReadLikeTheEngine)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");
	const QString sourceDir = tempDir.path() + "/Source";
	requireTrue(QDir().mkpath(sourceDir + "/sub dir"), "failed to create the source tree");
	auto writeText = [](const QString& path, const QString& body) {
		QFile f(path);
		expectTrue(f.open(QIODevice::WriteOnly | QIODevice::Text), QString("could not open %1 for write").arg(path));
		QTextStream ts(&f);
		ts << body;
	};
	qputenv("EAPO_XT_TEST_IMPORT_DIR", QDir::toNativeSeparators(sourceDir).toLocal8Bit());
	writeText(sourceDir + "/main.txt",
		"Include: \"sub dir\\child.txt\"\n"
		"Include: %EAPO_XT_TEST_IMPORT_DIR%\\by-variable.txt\n");
	writeText(sourceDir + "/sub dir/child.txt", "Preamp: -1 dB\n");
	writeText(sourceDir + "/by-variable.txt", "Preamp: -2 dB\n");

	const EqAPO::Import::ImportManifest manifest = EqAPO::Import::ConfigDependencyScanner::scan(
		sourceDir + "/main.txt", tempDir.path() + "/configdir");
	expectFalse(manifest.hasErrors, "a quoted include and one through a variable are both found");
	QStringList destinations;
	for (const auto& item : manifest.items)
		destinations.append(item.destRelative);
	expectTrue(destinations.contains("Source/sub dir/child.txt"), "the quoted include is imported");
	expectTrue(destinations.contains("Source/by-variable.txt"), "the include through a variable is imported");

	FileReferenceController quoted(QStringLiteral("include"), QStringLiteral("\"sub dir\\child.txt\""));
	quoted.resolveAgainstConfig(sourceDir + "/main.txt");
	expectPath(QDir::fromNativeSeparators(quoted.resolvedPath()), sourceDir + "/sub dir/child.txt");
	// Answered synchronously, as under the gallery's policy (the product asks
	// off the UI thread, audit #467 TD-59).
	FileReferenceController::Policy synchronous;
	synchronous.probePresenceInBackground = false;
	FileReferenceController::installPolicy(synchronous);
	const ReferenceCardState state = quoted.describe(QStringLiteral("none"));
	FileReferenceController::installPolicy(FileReferenceController::Policy());
	expectFalse(state.missing, "the card finds the quoted include");
	expectEqual(state.name, QStringLiteral("child.txt"), "and names it without the quotes");

	FileReferenceController variable(QStringLiteral("include"),
		QStringLiteral("%EAPO_XT_TEST_IMPORT_DIR%\\by-variable.txt"));
	variable.resolveAgainstConfig(sourceDir + "/main.txt");
	expectPath(QDir::fromNativeSeparators(variable.resolvedPath()), sourceDir + "/by-variable.txt");

	expectEqual(FileReferenceController::audioServiceProblem(
		QStringLiteral("\\\\nas\\irs\\room.wav"), sourceDir + "/main.txt"),
		QStringLiteral("The audio service only opens files on local drives"),
		"a file on a share is refused by the engine's rule, not reported as an ACL problem");
	FileReferenceController::Policy galleryPolicy;
	galleryPolicy.checkAudioService = false;
	FileReferenceController::installPolicy(galleryPolicy);
	expectTrue(FileReferenceController::audioServiceProblem(
		QStringLiteral("\\\\nas\\irs\\room.wav"), sourceDir + "/main.txt").isEmpty(),
		"the offscreen gallery's policy skips the verdict");
	FileReferenceController::installPolicy(FileReferenceController::Policy());
	qunsetenv("EAPO_XT_TEST_IMPORT_DIR");
}

TEST_CASE(EditorLogicTests, testConfigImport)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");

	QString surroundDir = tempDir.path() + "/Surround";
	expectTrue(QDir().mkpath(surroundDir), "failed to create Surround dir");

	auto writeText = [](const QString& path, const QString& body) {
		QFile f(path);
		expectTrue(f.open(QIODevice::WriteOnly | QIODevice::Text), QString("could not open %1 for write").arg(path));
		QTextStream ts(&f);
		ts << body;
	};
	auto writeBlob = [](const QString& path, int bytes) {
		QFile f(path);
		expectTrue(f.open(QIODevice::WriteOnly), QString("could not open %1 for write").arg(path));
		f.write(QByteArray(bytes, '\0'));
	};

	writeText(surroundDir + "/main.txt",
		"# main\n"
		"Preamp: -3 dB\n"
		"Include: child.txt\n"
		"Convolution: ir.wav\n");
	writeText(surroundDir + "/child.txt",
		"Convolution: nested.wav\n");
	writeBlob(surroundDir + "/ir.wav", 128);
	writeBlob(surroundDir + "/nested.wav", 64);

	EqAPO::Import::ImportManifest manifest = EqAPO::Import::ConfigDependencyScanner::scan(
		surroundDir + "/main.txt", tempDir.path() + "/configdir");

	expectFalse(manifest.hasErrors, "scan should not flag any errors for this tree");
	requireEqual(int(manifest.items.size()), 4, "expected root + child + ir + nested");

	expectEqual(manifest.items[0].kind, "Root", "first item must be the root config");
	expectEqual(manifest.items[0].destRelative, "Surround/main.txt", "root dest path");
	expectTrue(manifest.totalBytes > 0, "totalBytes should be positive");

	QStringList destRels;
	for (const auto& item : manifest.items)
		destRels.append(item.destRelative);
	expectTrue(destRels.contains("Surround/main.txt"), "root present in items");
	expectTrue(destRels.contains("Surround/child.txt"), "include child present");
	expectTrue(destRels.contains("Surround/ir.wav"), "ir wav present");
	expectTrue(destRels.contains("Surround/nested.wav"), "nested wav present");

	writeBlob(surroundDir + "/env.wav", 24);
	qputenv("EAPO_XT_TEST_IMPORT_IR", surroundDir.toUtf8());
	writeText(surroundDir + "/env.txt",
		"Convolution: \"%EAPO_XT_TEST_IMPORT_IR%/env.wav\"\n");
	auto environmentManifest = EqAPO::Import::ConfigDependencyScanner::scan(
		surroundDir + "/env.txt", tempDir.path() + "/configdir");
	expectFalse(environmentManifest.hasErrors,
		"scanner did not apply the engine's environment expansion policy");
	requireEqual(int(environmentManifest.items.size()), 2,
		"environment-expanded config collects its referenced IR");
	expectEqual(environmentManifest.items[1].sourceAbsolute,
		QDir::cleanPath(surroundDir + "/env.wav"),
		"scanner and engine resolve environment-expanded paths identically");

	const QString externalPlugin = tempDir.path() + "/plugins/Test Vst.dll";
	expectTrue(QDir().mkpath(QFileInfo(externalPlugin).absolutePath()),
		"failed to create external VST fixture directory");
	writeBlob(externalPlugin, 64);
	const QString vstLine = QStringLiteral("VSTPlugin: Library \"%1\" ChunkData \"QUJDRA==\"\n")
		.arg(QDir::toNativeSeparators(externalPlugin));
	writeText(surroundDir + "/vst.txt", vstLine);

	auto vstManifest = EqAPO::Import::ConfigDependencyScanner::scan(
		surroundDir + "/vst.txt", tempDir.path() + "/configdir");
	expectFalse(vstManifest.hasErrors,
		"an external VST reference should not block config import");
	requireEqual(int(vstManifest.items.size()), 1,
		"external VST binaries must not be copied with the config");
	requireEqual(int(vstManifest.externalReferences.size()), 1,
		"external VST reference should be recorded separately");
	expectPath(vstManifest.externalReferences[0], externalPlugin);

	const QString vstImportDir = tempDir.path() + "/vst-import";
	const auto vstImportResult = EqAPO::Import::ImportExecutor::execute(vstManifest, vstImportDir);
	expectTrue(vstImportResult.success, "VST config import should succeed without copying its DLL");
	expectEqual(vstImportResult.filesCopied, 1, "VST import copies only the config file");
	const auto importedVstConfig = ConfigFileCodec::readConfig(vstImportDir + "/Surround/vst.txt");
	expectTrue(importedVstConfig.ok, "imported VST config should be readable");
	requireEqual(int(importedVstConfig.lines.size()), 2, "imported VST config preserves its line and terminator");
	expectEqual(importedVstConfig.lines[0], vstLine.trimmed(),
		"imported config must retain the external VST Library reference");

	// Legacy configs use the system ANSI code page when a line is not valid
	// UTF-8. Pick a character that round-trips through this machine's ACP but
	// is invalid as standalone UTF-8, then prove the import scanner follows the
	// same decoding policy as the engine and ConfigFileCodec.
	const std::wstring ansiCandidates[] = {L"\u00e9", L"\uac00", L"\u0416", L"\u3042"};
	std::string ansiLeafBytes;
	std::wstring ansiLeafWide;
	for (const std::wstring& candidate : ansiCandidates)
	{
		std::string encoded = wintext::toNarrowString(candidate, CP_ACP);
		if (encoded.find('?') == std::string::npos
			&& wintext::toWideString(encoded, CP_ACP) == candidate
			&& QString::fromUtf8(encoded.data(), static_cast<qsizetype>(encoded.size())).contains(QChar::ReplacementCharacter))
		{
			ansiLeafBytes = "ansi-" + encoded + ".wav";
			ansiLeafWide = L"ansi-" + candidate + L".wav";
			break;
		}
	}

	if (!ansiLeafBytes.empty())
	{
		const QString ansiLeaf = QString::fromStdWString(ansiLeafWide);
		writeBlob(surroundDir + "/" + ansiLeaf, 32);

		QFile ansiConfig(surroundDir + "/ansi.txt");
		requireTrue(ansiConfig.open(QIODevice::WriteOnly), "could not create ANSI config");
		const std::string ansiLine = "Convolution: " + ansiLeafBytes + "\r\n";
		requireTrue(ansiConfig.write(ansiLine.data(), static_cast<qint64>(ansiLine.size()))
			== static_cast<qint64>(ansiLine.size()), "could not write ANSI config bytes");
		ansiConfig.close();

		auto ansiManifest = EqAPO::Import::ConfigDependencyScanner::scan(
			surroundDir + "/ansi.txt", tempDir.path() + "/configdir");
		expectFalse(ansiManifest.hasErrors, "scanner decoded an ANSI config differently from the engine");
		requireEqual(int(ansiManifest.items.size()), 2, "ANSI config collects its referenced IR");
		expectEqual(ansiManifest.items[1].sourceAbsolute,
			QDir::cleanPath(surroundDir + "/" + ansiLeaf), "ANSI reference resolves to the real file");
	}

	// Missing reference should surface as a non-fatal warning + hasErrors.
	writeText(surroundDir + "/broken.txt", "Convolution: does_not_exist.wav\n");
	auto broken = EqAPO::Import::ConfigDependencyScanner::scan(surroundDir + "/broken.txt", tempDir.path() + "/configdir");
	expectTrue(broken.hasErrors, "missing dependency must flag hasErrors");
	expectTrue(!broken.warnings.isEmpty(), "missing dependency must produce a warning");

	QString configDest = tempDir.path() + "/configdir";
	EqAPO::Import::ExecutionResult exec = EqAPO::Import::ImportExecutor::execute(manifest, configDest);
	expectTrue(exec.success, "executor should succeed on clean manifest");
	expectEqual(exec.filesCopied, 4, "executor must copy four files");

	expectTrue(QFile::exists(configDest + "/Surround/main.txt"), "main.txt missing after import");
	expectTrue(QFile::exists(configDest + "/Surround/child.txt"), "child.txt missing after import");
	expectTrue(QFile::exists(configDest + "/Surround/ir.wav"), "ir.wav missing after import");
	expectTrue(QFile::exists(configDest + "/Surround/nested.wav"), "nested.wav missing after import");

	// Re-executing should be idempotent (overwrites are allowed).
	EqAPO::Import::ExecutionResult exec2 = EqAPO::Import::ImportExecutor::execute(manifest, configDest);
	expectTrue(exec2.success, "second execute should also succeed");
	expectEqual(exec2.filesCopied, 4, "second execute should still report four copies");

	// A bare impulse-response file - the path the ConvolutionCardEditor
	// import button takes - scans to a single-item manifest rooted at the
	// file itself (no .txt recursion), keeping the source folder name as a
	// subdirectory so the copy lands at config/<folder>/<file>.
	QString convConfigDest = tempDir.path() + "/conv-configdir";
	EqAPO::Import::ImportManifest single = EqAPO::Import::ConfigDependencyScanner::scan(
		surroundDir + "/ir.wav", convConfigDest);
	expectFalse(single.hasErrors, "single wav scan should not flag errors");
	requireEqual(int(single.items.size()), 1, "single wav scan yields exactly one item");
	expectEqual(single.items[0].kind, "Root", "single wav item is the root");
	expectEqual(single.items[0].destRelative, "Surround/ir.wav", "single wav keeps its source folder");
	expectEqual(single.rootDest, "Surround/ir.wav", "single wav rootDest mirrors the item");

	EqAPO::Import::ExecutionResult singleExec = EqAPO::Import::ImportExecutor::execute(single, convConfigDest);
	expectTrue(singleExec.success, "single wav import should succeed");
	expectEqual(singleExec.filesCopied, 1, "single wav import copies exactly one file");
	expectTrue(QFile::exists(convConfigDest + "/Surround/ir.wav"), "ir.wav missing after single-file import");

	// A bare plugin binary - the path the VSTCardEditor import button takes
	// for a library the audio service cannot read - scans the same way: one
	// Root item, no recursion, the source folder kept as a subdirectory.
	// (The card then writes the copy's absolute path, because the engine
	// resolves relative Library references against the install VSTPlugins
	// directory, not the config directory.)
	writeBlob(surroundDir + "/TestPlugin.dll", 96);
	QString vstConfigDest = tempDir.path() + "/vst-configdir";
	EqAPO::Import::ImportManifest pluginSingle = EqAPO::Import::ConfigDependencyScanner::scan(
		surroundDir + "/TestPlugin.dll", vstConfigDest);
	expectFalse(pluginSingle.hasErrors, "single dll scan should not flag errors");
	requireEqual(int(pluginSingle.items.size()), 1, "single dll scan yields exactly one item");
	expectEqual(pluginSingle.items[0].kind, "Root", "single dll item is the root");
	expectEqual(pluginSingle.rootDest, "Surround/TestPlugin.dll", "single dll keeps its source folder");

	EqAPO::Import::ExecutionResult pluginExec = EqAPO::Import::ImportExecutor::execute(pluginSingle, vstConfigDest);
	expectTrue(pluginExec.success, "single dll import should succeed");
	expectTrue(QFile::exists(vstConfigDest + "/Surround/TestPlugin.dll"), "dll missing after single-file import");
}

// Audit #467 TD-01: QDir::relativeFilePath returns an absolute path when
// source and target are on different drives. Such a reference is outside the
// import root and must never become a destination.
TEST_CASE(EditorLogicTests, testConfigImportRejectsAnAbsoluteReferenceOnAnotherDrive)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");
	const QString sourceDir = tempDir.path() + QStringLiteral("/Source");
	requireTrue(QDir().mkpath(sourceDir), "failed to create source directory");

	const QString sourcePath = QDir::fromNativeSeparators(QFileInfo(sourceDir).absoluteFilePath());
	requireTrue(sourcePath.size() >= 3 && sourcePath.at(1) == QLatin1Char(':'),
		"temporary directory must be on a drive-letter path");
	const QChar otherDrive = sourcePath.at(0).toUpper() == QLatin1Char('C')
		? QLatin1Char('D') : QLatin1Char('C');
	const QString outside = QStringLiteral("%1:/IR/room.wav").arg(otherDrive);
	QFile config(sourceDir + QStringLiteral("/config.txt"));
	requireTrue(config.open(QIODevice::WriteOnly | QIODevice::Text), "failed to create source config");
	config.write(QStringLiteral("Convolution: %1\n").arg(outside).toUtf8());
	config.close();

	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scan(
		config.fileName(), tempDir.path() + QStringLiteral("/target"),
		EqAPO::Import::DestLayout::SourceFolderIsRoot);
	expectEqual(int(manifest.items.size()), 1,
		"an absolute reference on another drive is excluded from the manifest");
	bool warnedOutside = false;
	for (const QString& warning : manifest.warnings)
		warnedOutside = warnedOutside || warning.contains(QStringLiteral("outside the source folder"));
	expectTrue(warnedOutside, "the skipped absolute reference produces the outside-folder warning");
}

// Audit #467 TD-01: an untrusted manifest used to turn an absolute destination
// into the source path, delete that source, then fail to copy it onto itself.
TEST_CASE(EditorLogicTests, testConfigImportRejectsAnAbsoluteDestinationWithoutDeletingTheSource)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");
	const QString source = tempDir.path() + QStringLiteral("/room.wav");
	QFile sourceFile(source);
	requireTrue(sourceFile.open(QIODevice::WriteOnly), "failed to create source file");
	const QByteArray original("source-bytes");
	requireTrue(sourceFile.write(original) == original.size(), "failed to write source file");
	sourceFile.close();

	EqAPO::Import::ImportManifest manifest;
	manifest.rootSourceDir = tempDir.path();
	EqAPO::Import::ImportItem item;
	item.sourceAbsolute = source;
	item.destRelative = source;
	item.sizeBytes = original.size();
	manifest.items.append(item);

	const auto result = EqAPO::Import::ImportExecutor::execute(
		manifest, tempDir.path() + QStringLiteral("/config"));
	expectFalse(result.success, "an absolute destination is reported as an error");
	expectTrue(!result.errors.isEmpty(), "an absolute destination has an error message");
	const bool sourceStillExists = QFile::exists(source);
	expectTrue(sourceStillExists, "an absolute destination must never delete the source file");
	if (sourceStillExists)
	{
		QFile after(source);
		requireTrue(after.open(QIODevice::ReadOnly), "source file remains readable");
		expectEqual(QString::fromLatin1(after.readAll()), QString::fromLatin1(original),
			"an absolute destination leaves the source bytes unchanged");
	}
}

// Audit #467 TD-01: replacing a destination must not remove the old file until
// a complete new copy is ready to commit.
TEST_CASE(EditorLogicTests, testConfigImportCopyFailurePreservesTheExistingDestination)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");
	const QString source = tempDir.path() + QStringLiteral("/source.wav");
	QFile sourceFile(source);
	requireTrue(sourceFile.open(QIODevice::WriteOnly), "failed to create source file");
	sourceFile.write("new-content");
	sourceFile.close();

	const QString configDir = tempDir.path() + QStringLiteral("/config");
	requireTrue(QDir().mkpath(configDir), "failed to create config directory");
	const QString destination = configDir + QStringLiteral("/room.wav");
	QFile oldFile(destination);
	requireTrue(oldFile.open(QIODevice::WriteOnly), "failed to create existing destination");
	oldFile.write("old-content");
	oldFile.close();

	EqAPO::Import::ImportManifest manifest;
	manifest.rootSourceDir = tempDir.path();
	EqAPO::Import::ImportItem item;
	item.sourceAbsolute = source;
	item.destRelative = QStringLiteral("room.wav");
	item.sizeBytes = QFileInfo(source).size();
	manifest.items.append(item);
	requireTrue(QFile::remove(source), "source is removed after the manifest is built");

	const auto result = EqAPO::Import::ImportExecutor::execute(manifest, configDir);
	expectFalse(result.success, "a source that disappeared is reported as an error");
	const bool destinationStillExists = QFile::exists(destination);
	expectTrue(destinationStillExists, "a failed copy preserves the existing destination file");
	if (destinationStillExists)
	{
		QFile after(destination);
		requireTrue(after.open(QIODevice::ReadOnly), "existing destination remains readable");
		expectEqual(QString::fromLatin1(after.readAll()), QStringLiteral("old-content"),
			"a failed copy preserves the existing destination bytes");
	}
}

TEST_CASE(EditorLogicTests, testConfigImportRemovesOnlyCommittedSourcesAndPrunesEmptyFolders)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");
	const QString sourceDir = tempDir.path() + QStringLiteral("/source");
	const QString selectedDir = sourceDir + QStringLiteral("/selected");
	const QString unrelatedDir = sourceDir + QStringLiteral("/unrelated");
	requireTrue(QDir().mkpath(selectedDir) && QDir().mkpath(unrelatedDir),
		"failed to create source tree");
	QFile config(sourceDir + QStringLiteral("/config.txt"));
	requireTrue(config.open(QIODevice::WriteOnly | QIODevice::Text), "failed to create source config");
	config.write("Convolution: selected/room.wav\n");
	config.close();
	QFile selected(selectedDir + QStringLiteral("/room.wav"));
	requireTrue(selected.open(QIODevice::WriteOnly), "failed to create selected source");
	selected.write("selected-bytes");
	selected.close();
	QFile unrelated(unrelatedDir + QStringLiteral("/keep.wav"));
	requireTrue(unrelated.open(QIODevice::WriteOnly), "failed to create unrelated source");
	unrelated.write("unrelated-bytes");
	unrelated.close();

	const QString configDir = tempDir.path() + QStringLiteral("/config");
	const auto manifest = EqAPO::Import::ConfigDependencyScanner::scan(
		config.fileName(), configDir, EqAPO::Import::DestLayout::SourceFolderIsRoot);
	requireEqual(int(manifest.items.size()), 2, "root and referenced IR are selected");
	const auto result = EqAPO::Import::ImportExecutor::execute(
		manifest, configDir, EqAPO::Import::Disposition::RemoveSourcesAfterCopy);
	expectTrue(result.success, "move import succeeds");
	expectEqual(result.filesCopied, 2, "move import copies both selected files");
	expectEqual(result.filesRemovedFromSource, 2, "move import removes only committed originals");
	expectTrue(QFile::exists(configDir + QStringLiteral("/config.txt")),
		"moved config exists at destination");
	const QString destinationIr = configDir + QStringLiteral("/selected/room.wav");
	expectTrue(QFile::exists(destinationIr), "moved IR exists at destination");
	QFile copied(destinationIr);
	requireTrue(copied.open(QIODevice::ReadOnly), "moved IR is readable");
	expectEqual(QString::fromLatin1(copied.readAll()), QStringLiteral("selected-bytes"),
		"moved IR keeps its bytes");
	expectFalse(QFile::exists(config.fileName()), "committed config source is removed");
	expectFalse(QFile::exists(selected.fileName()), "committed IR source is removed");
	expectFalse(QDir(selectedDir).exists(), "source subfolder emptied by the move is pruned");
	expectTrue(QFile::exists(unrelated.fileName()), "unselected source file remains");
	expectTrue(QDir(unrelatedDir).exists(), "folder containing an unselected file remains");
}

TEST_CASE(EditorLogicTests, testConfigImportKeepsSourcesByDefault)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");
	const QString sourceDir = tempDir.path() + QStringLiteral("/source");
	requireTrue(QDir().mkpath(sourceDir), "failed to create source directory");
	const QString source = sourceDir + QStringLiteral("/room.wav");
	QFile sourceFile(source);
	requireTrue(sourceFile.open(QIODevice::WriteOnly), "failed to create source file");
	sourceFile.write("kept");
	sourceFile.close();

	EqAPO::Import::ImportManifest manifest;
	manifest.rootSourceDir = sourceDir;
	EqAPO::Import::ImportItem item;
	item.sourceAbsolute = source;
	item.destRelative = QStringLiteral("IR/room.wav");
	item.sizeBytes = QFileInfo(source).size();
	manifest.items.append(item);

	const auto result = EqAPO::Import::ImportExecutor::execute(
		manifest, tempDir.path() + QStringLiteral("/config"));
	expectTrue(result.success, "default import succeeds");
	expectTrue(QFile::exists(source), "default import keeps the user's source file");
}

// Audit #467 TD-02: a card may offer import for a file already below the
// config directory because LOCAL SERVICE cannot read its ACL. The executor
// must recognize that the source is already at its destination.
TEST_CASE(EditorLogicTests, testConfigImportReportsAFileAlreadyInPlace)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");
	const QString configDir = tempDir.path() + QStringLiteral("/config");
	const QString irDir = configDir + QStringLiteral("/IR");
	requireTrue(QDir().mkpath(irDir), "failed to create config IR directory");
	const QString source = irDir + QStringLiteral("/room.wav");
	QFile sourceFile(source);
	requireTrue(sourceFile.open(QIODevice::WriteOnly), "failed to create in-place source");
	const QByteArray original("in-place-bytes");
	requireTrue(sourceFile.write(original) == original.size(), "failed to write in-place source");
	sourceFile.close();

	EqAPO::Import::ImportManifest manifest;
	manifest.rootSourceDir = irDir;
	EqAPO::Import::ImportItem item;
	item.sourceAbsolute = source;
	item.destRelative = QStringLiteral("IR/room.wav");
	item.sizeBytes = original.size();
	manifest.items.append(item);

	const auto result = EqAPO::Import::ImportExecutor::execute(manifest, configDir);
	expectTrue(result.success, "an in-place import succeeds without copying");
	expectEqual(result.filesAlreadyInPlace, 1, "the in-place file is reported once");
	expectEqual(result.filesCopied, 0, "the in-place file is not copied");
	expectTrue(result.errors.isEmpty(), "the in-place file produces no error");
	expectTrue(QFile::exists(source), "the in-place file remains present");
	QFile after(source);
	requireTrue(after.open(QIODevice::ReadOnly), "the in-place file remains readable");
	expectEqual(QString::fromLatin1(after.readAll()), QString::fromLatin1(original),
		"the in-place file remains byte-identical");
}

TEST_CASE(EditorLogicTests, testLegacyMigrationScanAndPolicy)
{
	QTemporaryDir tempDir;
	requireTrue(tempDir.isValid(), "QTemporaryDir must be valid");

	// A legacy config root: config.txt reaching files through a nested
	// Include, a quoted Convolution path in a subfolder, and both
	// MultiConvolution forms (simple and factor mappings).
	QString legacyDir = tempDir.path() + "/config";
	expectTrue(QDir().mkpath(legacyDir + "/irs"), "failed to create legacy tree");

	auto writeText = [](const QString& path, const QString& body) {
		QFile f(path);
		expectTrue(f.open(QIODevice::WriteOnly | QIODevice::Text), QString("could not open %1 for write").arg(path));
		QTextStream ts(&f);
		ts << body;
	};
	auto writeBlob = [](const QString& path, int bytes) {
		QFile f(path);
		expectTrue(f.open(QIODevice::WriteOnly), QString("could not open %1 for write").arg(path));
		f.write(QByteArray(bytes, '\0'));
	};

	writeText(legacyDir + "/config.txt",
		"Preamp: -3 dB\n"
		"Include: upmix.txt\n"
		"MultiConvolution: L=0.5*0+1 R=1 irs/hrir.wav\n");
	writeText(legacyDir + "/upmix.txt",
		"Convolution: \"irs/room ir.wav\"\n"
		"MultiConvolution: C brir.wav\n");
	writeBlob(legacyDir + "/irs/hrir.wav", 96);
	writeBlob(legacyDir + "/irs/room ir.wav", 48);
	writeBlob(legacyDir + "/brir.wav", 32);

	// SourceFolderIsRoot maps the legacy folder 1:1 onto the target root:
	// no "config/" prefix on any destination.
	EqAPO::Import::ImportManifest manifest = EqAPO::Import::ConfigDependencyScanner::scan(
		legacyDir + "/config.txt", tempDir.path() + "/root",
		EqAPO::Import::DestLayout::SourceFolderIsRoot);
	expectFalse(manifest.hasErrors, "legacy scan should not flag errors");
	requireEqual(int(manifest.items.size()), 5, "expected config + include + three IRs");
	expectEqual(manifest.rootDest, "config.txt", "migration layout keeps the root name bare");

	QStringList destRels;
	for (const auto& item : manifest.items)
		destRels.append(item.destRelative);
	expectTrue(destRels.contains("config.txt"), "root config present without folder prefix");
	expectTrue(destRels.contains("upmix.txt"), "included file present");
	expectTrue(destRels.contains("irs/hrir.wav"), "factor-form MultiConvolution IR collected");
	expectTrue(destRels.contains("irs/room ir.wav"), "quoted Convolution path collected");
	expectTrue(destRels.contains("brir.wav"), "simple-form MultiConvolution IR collected");

	// The card editors' nested layout still prefixes the source folder name.
	EqAPO::Import::ImportManifest nested = EqAPO::Import::ConfigDependencyScanner::scan(
		legacyDir + "/config.txt", tempDir.path() + "/root");
	expectEqual(nested.rootDest, "config/config.txt", "default layout keeps the folder prefix");

	// Policy: the pure classification the elevated hook acts on.
	using Policy = EqAPO::Import::LegacyMigrationPolicy;
	const QString stableRoot = Policy::stableConfigRoot("C:/Users/me/AppData/Local");
	expectEqual(stableRoot, "C:/Users/me/AppData/Local/EqualizerAPO-XT/config",
		"stable root derives from LOCALAPPDATA");
	expectTrue(Policy::stableConfigRoot(" ").isEmpty(), "missing LOCALAPPDATA yields no root");

	expectTrue(Policy::isVolatileXtConfigDir("C:\\Users\\me\\AppData\\Local\\EqualizerAPO-XT-x64-avx2\\current\\config"),
		"variant current\\config is volatile");
	expectTrue(Policy::isVolatileXtConfigDir("D:/apps/EqualizerAPO-XT-arm64-neon/current/config"),
		"volatile match is drive- and variant-independent");
	expectFalse(Policy::isVolatileXtConfigDir("C:\\Program Files\\EqualizerAPO\\config"),
		"legacy Program Files root is not volatile");
	expectFalse(Policy::isVolatileXtConfigDir(stableRoot), "the stable root itself is not volatile");

	expectTrue(Policy::hasLegacyApoFolderName("C:\\Program Files\\EqualizerAPO\\config"),
		"upstream default install dir is recognized by name");
	expectTrue(Policy::hasLegacyApoFolderName("D:/tools/Equalizer APO/config"),
		"spaced installer folder name is recognized on any drive");
	expectFalse(Policy::hasLegacyApoFolderName("C:\\Users\\me\\AppData\\Local\\EqualizerAPO-XT\\config"),
		"the XT stable root is not mistaken for the legacy install");
	expectFalse(Policy::hasLegacyApoFolderName("D:\\my configs"),
		"unrelated folders carry no legacy name");

	expectTrue(Policy::classify(QString(), stableRoot, false, false, true) == Policy::Action::AdoptStableRoot,
		"no ConfigPath adopts the stable root");
	expectTrue(Policy::classify("c:\\users\\me\\appdata\\local\\equalizerapo-xt\\CONFIG", stableRoot, false, false, false)
		== Policy::Action::AlreadyOurs, "case-insensitive match on the stable root");
	expectTrue(Policy::classify("C:\\Program Files\\EqualizerAPO\\config", stableRoot, true, false, true)
		== Policy::Action::MigrateLegacy, "legacy markers trigger the import");
	expectTrue(Policy::classify("C:\\Users\\me\\AppData\\Local\\EqualizerAPO-XT-x64-avx2\\current\\config",
		stableRoot, false, true, true) == Policy::Action::MigrateVolatileXt, "volatile XT dir is rescued");
	expectTrue(Policy::classify("D:\\my configs", stableRoot, false, false, true) == Policy::Action::RespectCustom,
		"a user-chosen folder is left alone");
	expectTrue(Policy::classify("C:\\Program Files\\EqualizerAPO\\config", stableRoot, true, false, false)
		== Policy::Action::AdoptStableRoot, "a missing non-empty ConfigPath adopts the stable root");
	expectTrue(Policy::classify(stableRoot, stableRoot, false, false, false) == Policy::Action::AlreadyOurs,
		"the stable root remains ours before its directory is created");

	// Saved open/recent file paths remap into the stable root after a
	// migration; anything outside the migrated folder stays untouched.
	expectEqual(Policy::remapUnderRoot("C:\\Program Files\\EqualizerAPO\\config\\Surround\\ir.wav",
		"C:\\Program Files\\EqualizerAPO\\config", stableRoot),
		stableRoot + "/Surround/ir.wav", "nested legacy path remaps with its subfolder");
	expectEqual(Policy::remapUnderRoot("c:/program files/equalizerapo/CONFIG/config.txt",
		"C:\\Program Files\\EqualizerAPO\\config", stableRoot),
		stableRoot + "/config.txt", "remap is case- and separator-insensitive");
	expectTrue(Policy::remapUnderRoot("C:\\Program Files\\EqualizerAPO\\configX\\a.txt",
		"C:\\Program Files\\EqualizerAPO\\config", stableRoot).isEmpty(),
		"a sibling folder sharing the prefix is not remapped");
	expectTrue(Policy::remapUnderRoot("D:\\elsewhere\\a.txt",
		"C:\\Program Files\\EqualizerAPO\\config", stableRoot).isEmpty(),
		"paths outside the migrated root are untouched");
	expectTrue(Policy::remapUnderRoot("C:\\Program Files\\EqualizerAPO\\config",
		"C:\\Program Files\\EqualizerAPO\\config", stableRoot).isEmpty(),
		"the root itself is not a file to remap");
}
