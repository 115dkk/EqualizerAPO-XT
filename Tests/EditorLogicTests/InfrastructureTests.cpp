#include "EditorLogicTestSupport.h"

#include <new>
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
#include "helpers/MemoryHelper.h"
// The roster is the one list of which skins exist, so what is worth pinning is
// that it is complete and that everything derived from it lands on a real skin.
// Missing a skin used to be silent: resolveId() falls back to Studio, so a skin
// that was registered but forgotten in one of eighteen places was drawn as Studio
// with no error anywhere.
void testTheSkinRosterIsTheOneList()
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

void testSkinTokensCarryExplicitMode()
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
void testTokenSubstitutionOffersAnAlphaForm()
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

void testEverySkinSheetResolvesAllThemeTokens()
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

void testEditableValueTextUsesDisplayedDecimalFormatFirst()
{
	const QLocale german(QLocale::German, QLocale::Germany);
	double value = 0.0;
	expectTrue(parseEditableValueText(QStringLiteral("12.345"), german, &value),
		"dot-decimal text parses under a grouping-dot locale");
	expectTrue(qAbs(value - 12.345) < 0.000001,
		"dot-decimal text keeps the displayed C-locale meaning");
	expectTrue(parseEditableValueText(QStringLiteral("12,5"), german, &value),
		"system-locale decimal text remains accepted as a fallback");
	expectTrue(qAbs(value - 12.5) < 0.000001,
		"system-locale fallback keeps its decimal meaning");
}

void testBenchmarkBatchPlanUsesOnlyComparableFullBatches()
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

void testFileReferenceControllerOwnsPathState()
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

	const QString baseDirectory = root.filePath(QStringLiteral("config/deep"));
	const QString farSelection = root.filePath(QStringLiteral("outside/plugin.dll"));
	expectPath(FileReferenceController::displayPathForBaseDirectory(
		baseDirectory, farSelection), farSelection);
}

void testAnalysisWorkerRecovery()
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

void testMemoryHelperConstructReleasesStorageWhenConstructorThrows()
{
	struct ThrowingConstructor
	{
		ThrowingConstructor()
		{
			throw std::runtime_error("constructor failure");
		}
	};

	// The counters live in MemoryHelper itself; this binary links Common.lib
	// whole-archive and therefore cannot substitute its own alloc()/free().
	MemoryHelper::resetAllocationCountsForTesting();
	bool threw = false;
	try
	{
		MemoryHelper::construct<ThrowingConstructor>();
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}

	expectTrue(threw, "construct propagates a constructor exception");
	expectEqual((int)MemoryHelper::allocationCountForTesting(), 1, "construct allocates storage once");
	expectEqual((int)MemoryHelper::freeCountForTesting(), 1, "construct releases storage when construction fails");
}
