/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "EditorLogicTestSupport.h"

#include <new>
#include <atomic>
#include <thread>
#include "Editor/helpers/CrashHandler.h"
#include "version.h"
#include <stdexcept>

#include <QDir>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "Benchmark/BatchPlan.h"
#include "Editor/helpers/AnalysisWorkerRecovery.h"
#include "Editor/skins/SkinThemeData.h"
#include "Editor/widgets/EditableValueText.h"
#include "Editor/widgets/cards/FileReferenceController.h"
#include "runtime/memory/AlignedMemory.h"
#include "runtime/concurrency/ShutdownSequence.h"

TEST_CASE(EditorLogicTests, testShutdownSequenceAppliesOnlyAfterEveryWait)
{
	std::vector<std::wstring> calls;
	const bool finished = ShutdownSequence::run({
		{ [] { return L"plug-in loader"; }, [&] { calls.push_back(L"loader"); return true; } },
		{ [] { return L"background probe"; }, [&] { calls.push_back(L"probe"); return true; } },
		{ [] { return L"update download"; }, [&] { calls.push_back(L"download"); return true; } }
	}, 17, [&] { calls.push_back(L"apply"); },
		[&](const std::wstring&) { calls.push_back(L"log"); },
		[&](int) { calls.push_back(L"exit"); });
	expectTrue(finished, QStringLiteral("completed shutdown returns normally"));
	expectTrue(calls == std::vector<std::wstring>{L"loader", L"probe", L"download", L"apply"},
		QStringLiteral("update applies only after the ordered waits; no timeout log or exit"));
}

TEST_CASE(EditorLogicTests, testShutdownSequenceTimeoutSkipsApplyAndLaterWaits)
{
	for (const int code : {0, 17})
	{
		for (int timeoutIndex = 0; timeoutIndex < 3; ++timeoutIndex)
		{
			int waits = 0;
			int exits = 0;
			int actualCode = -1;
			int applies = 0;
			std::vector<std::wstring> logs;
			std::vector<std::wstring> calls;
			std::wstring loaderName = L"plug-in loader";
			const auto wait = [&] { return waits++ != timeoutIndex; };
			const bool finished = ShutdownSequence::run({
				{ [&] { return loaderName; }, [&] { loaderName = L"loading plug-in C:/fixture/slow.dll"; return wait(); } },
				{ [] { return L"background probe"; }, wait },
				{ [] { return L"update download"; }, wait }
			}, code, [&] { ++applies; }, [&](const std::wstring& line) { logs.push_back(line); calls.push_back(L"log"); },
				[&](int value) { ++exits; actualCode = value; calls.push_back(L"exit"); });
			const std::wstring names[] = {loaderName, L"background probe", L"update download"};
			expectFalse(finished, QStringLiteral("timed-out shutdown cannot continue normally"));
			expectEqual(waits, timeoutIndex + 1, QStringLiteral("no later waits run after the first timeout"));
			expectEqual(applies, 0, QStringLiteral("timeout never applies an update or launches its coordinator"));
			expectEqual(exits, 1, QStringLiteral("timeout requests hard exit exactly once"));
			expectEqual(actualCode, code, QStringLiteral("hard exit preserves the decided process code"));
			expectTrue(logs == std::vector<std::wstring>{L"Shutdown timed out: " + names[timeoutIndex]},
				QStringLiteral("one timeout line names the running worker or plug-in path"));
			expectTrue(calls == std::vector<std::wstring>{L"log", L"exit"}, QStringLiteral("timeout logs before hard exit"));
		}
	}
}

TEST_CASE(EditorLogicTests, testCrashBreadcrumbConcurrentWriters)
{
	const std::wstring first(255, L'A');
	const std::wstring second(255, L'B');
	CrashHandler::setBreadcrumb(first);
	std::atomic<bool> go{false};
	std::atomic<int> finished{0};
	auto write = [&](const std::wstring& value) {
		while (!go.load())
			std::this_thread::yield();
		for (int i = 0; i < 20000; ++i)
			CrashHandler::setBreadcrumb(value);
		++finished;
	};
	std::thread one([&] { write(first); });
	std::thread two([&] { write(second); });
	go.store(true);
	bool complete = true;
	do
	{
		wchar_t snapshot[256];
		CrashHandler::copyBreadcrumb(snapshot);
		const std::wstring value(snapshot);
		complete = complete && (value == first || value == second || value == L"(breadcrumb changing)");
	}
	while (finished.load() != 2);
	one.join();
	two.join();
	expectTrue(complete, QStringLiteral("concurrent breadcrumb snapshot is never a mixed string"));
	CrashHandler::setBreadcrumb(L"opening file config.txt");
	wchar_t snapshot[256];
	CrashHandler::copyBreadcrumb(snapshot);
	expectEqual(QString::fromWCharArray(snapshot), QStringLiteral("opening file config.txt"),
		QStringLiteral("snapshot records the complete last file action"));
}

TEST_CASE(EditorLogicTests, testCrashArtifactNamesKeepEditorCompatibility)
{
	wchar_t name[128];
	CrashHandler::formatArtifactName(name, L"Editor", 42, 7, L"dmp");
	const QString suffix = QStringLiteral("-%1.%2.%3-42-7").arg(MAJOR).arg(MINOR).arg(REVISION);
	expectEqual(QString::fromWCharArray(name), QStringLiteral("Editor") + suffix + ".dmp",
		QStringLiteral("Editor dump pattern is unchanged"));
	CrashHandler::formatArtifactName(name, L"DeviceSelector", 42, 7, L"txt");
	expectEqual(QString::fromWCharArray(name), QStringLiteral("DeviceSelector") + suffix + ".txt",
		QStringLiteral("DeviceSelector report names its own product"));
}

// The roster is the one list of which skins exist, so what is worth pinning is
// that it is complete and that everything derived from it lands on a real skin.
// Missing a skin used to be silent: resolveId() falls back to Studio, so a skin
// that was registered but forgotten in one of eighteen places was drawn as Studio
// with no error anywhere.
TEST_CASE(EditorLogicTests, testTheSkinRosterIsTheOneList)
{
	const QVector<SkinThemeData::SkinEntry>& roster = SkinThemeData::roster();
	requireTrue(roster.size() >= 5, "the roster carries the five built-in skins");
	expectTrue(roster.first().id == QStringLiteral("studio"),
		"studio is first, which is what an unknown id falls back to and what the default is");

	QSet<QString> seen;
	for (const SkinThemeData::SkinEntry& skin : roster)
	{
		expectFalse(skin.id.isEmpty(), "every entry has an id");
		expectFalse(skin.qssBaseName.isEmpty(), "every entry names its style sheet");
		expectTrue(skin.tokens != nullptr, "every entry carries its token table");
		expectFalse(seen.contains(skin.id),
			QStringLiteral("%1 appears once").arg(skin.id));
		seen.insert(skin.id);

		expectTrue(SkinThemeData::resolveId(skin.id) == skin.id,
			QStringLiteral("%1 is its own canonical id").arg(skin.id));
		expectTrue(SkinThemeData::entry(skin.id).id == skin.id,
			QStringLiteral("%1 resolves back to its own entry").arg(skin.id));
	}

	const QStringList ids = SkinThemeData::ids();
	requireTrue(ids.size() == roster.size(), "ids() has one entry per roster skin, in the same order");
	for (int index = 0; index < ids.size(); index++)
		expectTrue(ids[index] == roster[index].id, "ids() preserves the roster order, which is the display order");

	// The two stored aliases from earlier releases, which are the only ids that
	// cannot be derived from the roster.
	expectTrue(SkinThemeData::resolveId(QStringLiteral("glassy")) == QStringLiteral("studio"),
		"the glassy alias still resolves, because it is what old registries hold");
	expectTrue(SkinThemeData::resolveId(QStringLiteral("industrial")) == QStringLiteral("rack"),
		"and so does industrial");
	expectTrue(SkinThemeData::resolveId(QStringLiteral("no such skin")) == roster.first().id,
		"an unknown id falls back to the first entry rather than producing no skin at all");
}

TEST_CASE(EditorLogicTests, testSkinTokensCarryExplicitMode)
{
	const QStringList skinIds = SkinThemeData::ids();
	for (const QString& skinId : skinIds)
	{
		expectTrue(SkinThemeData::tokens(skinId, true).dark,
			QStringLiteral("%1 dark tokens carry dark=true").arg(skinId));
		expectFalse(SkinThemeData::tokens(skinId, false).dark,
			QStringLiteral("%1 light tokens carry dark=false").arg(skinId));
	}
}

// The alpha form a sheet needs to hold a token at partial opacity. QSS has no
// variables and its rgba() takes three numbers, so without this a sheet had to
// write the palette value out by hand and a token change stopped reaching it.
TEST_CASE(EditorLogicTests, testTokenSubstitutionOffersAnAlphaForm)
{
	const SkinTokens tokens = SkinThemeData::tokens(QStringLiteral("studio"), true);
	const QColor accent(tokens.accent);
	requireTrue(accent.isValid(), "the studio accent token parses as a colour");

	const QString resolved = SkinThemeData::substituteTokens(
		QStringLiteral("QWidget { background: rgba(@ACCENT_RGB@, 0.30); color: @ACCENT@; }"), tokens);

	expectTrue(resolved.contains(QStringLiteral("rgba(%1, %2, %3, 0.30)")
			.arg(accent.red()).arg(accent.green()).arg(accent.blue())),
		"the alpha form expands to the token's three channels, so rgba() gets numbers");
	expectTrue(resolved.contains(QStringLiteral("color: ") + tokens.accent),
		"and the plain form still expands to the hex value");
	expectFalse(resolved.contains(QStringLiteral("_RGB")),
		"the longer sentinel is replaced before the shorter one, or the hex value would be left with _RGB stuck to it");

	// A font family is not a colour and has no channels; asking for its alpha form
	// has to leave the sheet alone rather than produce something that would make
	// the whole rule invalid.
	const QString fontSheet = SkinThemeData::substituteTokens(
		QStringLiteral("QWidget { font-family: \"@FONT@\"; }"), tokens);
	expectTrue(fontSheet.contains(tokens.fontFamily), "the font token still resolves");
}

TEST_CASE(EditorLogicTests, testEverySkinSheetResolvesAllThemeTokens)
{
	QDir repoRoot(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
	requireTrue(repoRoot.cdUp(), "skin-token test reaches the tests directory");
	requireTrue(repoRoot.cdUp(), "skin-token test reaches the repository root");
	const QStringList skinIds = SkinThemeData::ids();
	const QRegularExpression unresolved(QStringLiteral("@[A-Z_0-9]+@"));
	for (const QString& skinId : skinIds)
	{
		for (bool dark : { false, true })
		{
			const QString resource = SkinThemeData::qssResource(skinId, dark);
			const QString sourcePath = repoRoot.filePath(
				QStringLiteral("Editor/skins/%1/qss/%2")
					.arg(skinId, QFileInfo(resource).fileName()));
			QFile file(sourcePath);
			expectTrue(file.open(QIODevice::ReadOnly | QIODevice::Text),
				QStringLiteral("loads %1 source sheet").arg(resource));
			const QString resolved = SkinThemeData::substituteTokens(
				QString::fromUtf8(file.readAll()), SkinThemeData::tokens(skinId, dark));
			expectFalse(unresolved.match(resolved).hasMatch(),
				QStringLiteral("%1 leaves no unresolved @TOKEN@ sentinel").arg(resource));
		}
	}
}

TEST_CASE(EditorLogicTests, testEditableValueTextUsesDisplayedDecimalFormatFirst)
{
	const QLocale german(QLocale::German, QLocale::Germany);
	double value = 0.0;
	expectTrue(parseEditableValueText(QStringLiteral("12.345"), german, &value),
		"dot-decimal text parses under a grouping-dot locale");
	expectNear(value, 12.345, 0.000001,
		"dot-decimal text keeps the displayed C-locale meaning");
	expectTrue(parseEditableValueText(QStringLiteral("12,5"), german, &value),
		"system-locale decimal text remains accepted as a fallback");
	expectNear(value, 12.5, 0.000001,
		"system-locale fallback keeps its decimal meaning");
}

TEST_CASE(EditorLogicTests, testBenchmarkBatchPlanUsesOnlyComparableFullBatches)
{
	const BenchmarkBatchPlan partial = planBenchmarkBatches(1000, 480);
	expectEqual(partial.processedFrames, 960u,
		"benchmark processes only full fixed-size batches");
	expectEqual(partial.trimmedFrames, 40u,
		"benchmark reports the excluded partial tail");

	const BenchmarkBatchPlan exact = planBenchmarkBatches(960, 480);
	expectEqual(exact.processedFrames, 960u,
		"batch-aligned benchmark lengths remain unchanged");
	expectEqual(exact.trimmedFrames, 0u,
		"batch-aligned benchmark lengths trim nothing");
}

TEST_CASE(EditorLogicTests, testFileReferenceControllerOwnsPathState)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "file-reference test creates a temporary root");
	QDir root(temp.path());
	requireTrue(root.mkpath(QStringLiteral("config/irs")),
		"file-reference test creates the config dependency directory");
	requireTrue(root.mkpath(QStringLiteral("outside")),
		"file-reference test creates the external directory");
	const QString configPath = root.filePath(QStringLiteral("config/config.txt"));
	const QString impulsePath = root.filePath(QStringLiteral("config/irs/room.wav"));
	QFile impulse(impulsePath);
	requireTrue(impulse.open(QIODevice::WriteOnly),
		"file-reference test creates the resolved dependency");
	impulse.close();

	// The synchronous answer the gallery's policy asks for; the product asks
	// off the UI thread (testFileReferenceControllerChecksPresenceInBackground).
	FileReferenceController::Policy synchronous;
	synchronous.probePresenceInBackground = false;
	FileReferenceController::installPolicy(synchronous);

	FileReferenceController reference(
		QStringLiteral("convolution"), QStringLiteral("irs/room.wav"));
	reference.resolveAgainstConfig(configPath);
	ReferenceCardState state = reference.describe(QStringLiteral("No file selected"));
	expectFalse(state.missing, "controller resolves a config-relative reference");
	expectEqual(state.name, QStringLiteral("room.wav"),
		"controller derives the reference display name");
	expectEqual(state.directory, QDir::toNativeSeparators(QStringLiteral("irs")),
		"controller preserves the as-written relative directory");
	expectPath(state.fullPath, impulsePath);

	reference.setWrittenPath(QStringLiteral("irs/missing.wav"));
	reference.resolveAgainstConfig(configPath);
	state = reference.describe(QStringLiteral("No file selected"));
	expectTrue(state.missing, "controller owns and reports an edited missing path");
	expectTrue(state.fullPath.isEmpty(), "missing references do not expose a clickable path");
	FileReferenceController::installPolicy(FileReferenceController::Policy());

	const QString baseDirectory = root.filePath(QStringLiteral("config/deep"));
	const QString farSelection = root.filePath(QStringLiteral("outside/plugin.dll"));
	expectPath(FileReferenceController::displayPathForBaseDirectory(
		baseDirectory, farSelection), farSelection);

	// A picked file becomes the reference by the same rule (the card's VST
	// chooser and chooseExistingFile both go through it).
	reference.adoptChosenFile(farSelection, baseDirectory);
	expectEqual(reference.writtenPath(), QDir::toNativeSeparators(farSelection),
		"a far pick is written absolute");
	expectEqual(reference.resolvedPath(), QDir::toNativeSeparators(farSelection),
		"a pick resolves to the picked file");
	reference.adoptChosenFile(impulsePath, root.filePath(QStringLiteral("config")));
	expectEqual(reference.writtenPath(), QDir::toNativeSeparators(QStringLiteral("irs/room.wav")),
		"a pick beneath the base directory is written relative");
	reference.adoptChosenFile(impulsePath, QString());
	expectEqual(reference.writtenPath(), QDir::toNativeSeparators(impulsePath),
		"a pick without a base directory is written as picked");

	// Both VST rows show a loaded library by one rule (audit #422 TD-56): the
	// cards' climb rule against the VSTPlugins directory, with the absolute
	// fallback keeping the library's own spelling.
	const QString pluginsDirectory = QStringLiteral("C:/Program Files/EqualizerAPO/VSTPlugins");
	expectEqual(FileReferenceController::displayPathForLibrary(
		QStringLiteral("C:\\Program Files\\EqualizerAPO\\VSTPlugins\\Comp\\comp.dll"), pluginsDirectory),
		QStringLiteral("Comp\\comp.dll"), "a library beneath VSTPlugins is shown relative");
	expectEqual(FileReferenceController::displayPathForLibrary(
		QStringLiteral("C:\\Program Files\\EqualizerAPO\\Shared\\comp.dll"), pluginsDirectory),
		QStringLiteral("..\\Shared\\comp.dll"), "one level above VSTPlugins is still relative");
	expectEqual(FileReferenceController::displayPathForLibrary(
		QStringLiteral("C:/Program Files/Common Files/VST3/comp.vst3"), pluginsDirectory),
		QStringLiteral("C:/Program Files/Common Files/VST3/comp.vst3"),
		"a far library keeps its own spelling");
	expectEqual(FileReferenceController::displayPathForLibrary(QString(), pluginsDirectory), QString(),
		"no library shows no path");
}

// The product's describe() does not touch the file system (audit #467 TD-59):
// until askPresence has an answer the card is checking, never missing and
// never a confirmed file; an empty reference is missing without a question.
TEST_CASE(EditorLogicTests, testFileReferenceControllerChecksPresenceInBackground)
{
	QTemporaryDir temp;
	requireTrue(temp.isValid(), "presence test creates a temporary root");
	const QString impulsePath = QDir(temp.path()).filePath(QStringLiteral("room.wav"));
	QFile impulse(impulsePath);
	requireTrue(impulse.open(QIODevice::WriteOnly), "presence test creates the file");
	impulse.close();

	FileReferenceController reference(QStringLiteral("convolution"), QStringLiteral("room.wav"));
	reference.resolveAgainstConfig(QDir(temp.path()).filePath(QStringLiteral("config.txt")));
	const ReferenceCardState state = reference.describe(QStringLiteral("No file selected"));
	expectTrue(state.checking, "an existing file is checking until the background answer");
	expectFalse(state.missing, "checking is not missing");
	expectTrue(state.fullPath.isEmpty(), "checking exposes no confirmed path");
	expectEqual(state.name, QStringLiteral("room.wav"), "the name is shown while checking");

	FileReferenceController empty(QStringLiteral("convolution"), QString());
	const ReferenceCardState emptyState = empty.describe(QStringLiteral("No file selected"));
	expectTrue(emptyState.missing, "an empty reference is missing at once");
	expectFalse(emptyState.checking, "and nothing is asked about it");
}

// The flow the convolution cards hand to the controller (audit #467 TD-40):
// typed text commits once, and resolution follows the attached configuration.
TEST_CASE(EditorLogicTests, testFileReferenceControllerCommitsForItsCard)
{
	FileReferenceController reference(QStringLiteral("convolution"), QStringLiteral("irs/room.wav"));
	expectTrue(reference.resolvedAbsolutePath().isEmpty(), "without a configuration nothing resolves");

	QString configPath = QStringLiteral("C:/EqualizerAPO/config/config.txt");
	int commits = 0;
	reference.attach([&configPath]() { return configPath; }, [&commits]() { ++commits; });
	expectPath(QDir::fromNativeSeparators(reference.resolvedAbsolutePath()),
		QStringLiteral("C:/EqualizerAPO/config/irs/room.wav"));

	reference.commitWrittenPath(QStringLiteral("  other.wav "));
	expectEqual(commits, 1, "typed text commits once");
	expectEqual(reference.writtenPath(), QStringLiteral("other.wav"), "the typed text is trimmed");
	configPath = QStringLiteral("D:/Other/config.txt");
	expectPath(QDir::fromNativeSeparators(reference.resolvedAbsolutePath()), QStringLiteral("D:/Other/other.wav"));
}

TEST_CASE(EditorLogicTests, testAnalysisWorkerRecovery)
{
	bool failurePublished = false;
	try
	{
		const bool succeeded = AnalysisWorkerRecovery::run(
			[] { throw std::bad_alloc(); },
			[&](const char*) { failurePublished = true; });
		expectFalse(succeeded, "failed analysis iteration reported success");
	}
	catch (...)
	{
		harness.fail("analysis iteration exception escaped and would stop the worker");
	}
	expectTrue(failurePublished, "analysis failure was not published");

	bool nextIterationRan = false;
	const bool recovered = AnalysisWorkerRecovery::run(
		[&] { nextIterationRan = true; },
		[](const char*) {});
	expectTrue(recovered, "next analysis iteration did not recover");
	expectTrue(nextIterationRan, "analysis worker did not accept work after a failure");
}

TEST_CASE(EditorLogicTests, testMemoryHelperConstructReleasesStorageWhenConstructorThrows)
{
	struct ThrowingConstructor
	{
		ThrowingConstructor()
		{
			throw std::runtime_error("constructor failure");
		}
	};

	// The counters live in AlignedMemory itself; this binary links Common.lib
	// whole-archive and therefore cannot substitute its own alloc()/free().
	AlignedMemory::resetAllocationCountsForTesting();
	bool threw = false;
	try
	{
		AlignedMemory::construct<ThrowingConstructor>();
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}

	expectTrue(threw, "construct propagates a constructor exception");
	expectEqual((int)AlignedMemory::allocationCountForTesting(), 1, "construct allocates storage once");
	expectEqual((int)AlignedMemory::freeCountForTesting(), 1, "construct releases storage when construction fails");
}
