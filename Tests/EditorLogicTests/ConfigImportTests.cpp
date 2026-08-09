/*
	This file is part of EqualizerAPO-XT.

	Config import and migration: config-relative convolution path
	resolution, the dependency scanner and import executor, and the
	legacy-install migration policy the elevated hook acts on.
*/

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include "Editor/ConfigFileCodec.h"
#include "Editor/helpers/ConvolutionPathHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportExecutor.h"
#include "Editor/import/ImportManifest.h"
#include "Editor/import/LegacyMigrationPolicy.h"
#include "text/StringHelper.h"

#include "EditorLogicTestSupport.h"

void testConvolutionPathHelper()
{
	const QString configPath = "C:/EqualizerAPO/config/config.txt";
	qputenv("EAPO_XT_TEST_IR_DIR", "C:\\Impulse Responses");

	expectPath(
		ConvolutionPathHelper::absolutePathForConfig(configPath, "irs/room.wav"),
		"C:/EqualizerAPO/config/irs/room.wav");
	expectPath(
		ConvolutionPathHelper::absolutePathForConfig(configPath, "C:/Impulse/room.wav"),
		"C:/Impulse/room.wav");
	expectTrue(
		ConvolutionPathHelper::absolutePathForConfig(configPath, QString()).isEmpty(),
		"empty convolution path should remain empty");
	expectPath(
		ConvolutionPathHelper::absolutePathForConfig(
			configPath, "\"%EAPO_XT_TEST_IR_DIR%\\quoted room.wav\""),
		"C:/Impulse Responses/quoted room.wav");

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
		ConvolutionPathHelper::relativePathLooksContainedLexically("irs/room.wav"),
		"relative path inside config directory was rejected");
	expectFalse(
		ConvolutionPathHelper::relativePathLooksContainedLexically("../shared/room.wav"),
		"parent-directory relative path was accepted");
	expectFalse(
		ConvolutionPathHelper::relativePathLooksContainedLexically("C:/Impulse/room.wav"),
		"absolute path was accepted as relative");
}

void testConfigImport()
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
		std::string encoded = StringHelper::toString(candidate, CP_ACP);
		if (encoded.find('?') == std::string::npos
			&& StringHelper::toWString(encoded, CP_ACP) == candidate
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
}

void testLegacyMigrationScanAndPolicy()
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

	expectTrue(Policy::classify(QString(), stableRoot, false, false) == Policy::Action::AdoptStableRoot,
		"no ConfigPath adopts the stable root");
	expectTrue(Policy::classify("c:\\users\\me\\appdata\\local\\equalizerapo-xt\\CONFIG", stableRoot, false, false)
		== Policy::Action::AlreadyOurs, "case-insensitive match on the stable root");
	expectTrue(Policy::classify("C:\\Program Files\\EqualizerAPO\\config", stableRoot, true, false)
		== Policy::Action::MigrateLegacy, "legacy markers trigger the import");
	expectTrue(Policy::classify("C:\\Users\\me\\AppData\\Local\\EqualizerAPO-XT-x64-avx2\\current\\config",
		stableRoot, false, true) == Policy::Action::MigrateVolatileXt, "volatile XT dir is rescued");
	expectTrue(Policy::classify("D:\\my configs", stableRoot, false, false) == Policy::Action::RespectCustom,
		"a user-chosen folder is left alone");

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
