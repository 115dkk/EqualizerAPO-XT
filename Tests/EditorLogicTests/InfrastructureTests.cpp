#include "EditorLogicTestSupport.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "Benchmark/BatchPlan.h"
#include "Editor/skins/SkinThemeData.h"
#include "Editor/widgets/EditableValueText.h"
#include "Editor/widgets/cards/FileReferenceController.h"
#include "helpers/OwnedBackgroundTask.h"

void testOwnedBackgroundTaskJoinsAndStartsOnlyOnce()
{
	OwnedBackgroundTask task;
	std::promise<void> enteredPromise;
	std::future<void> entered = enteredPromise.get_future();
	std::promise<void> releasePromise;
	std::shared_future<void> release = releasePromise.get_future().share();
	std::atomic<bool> completed{ false };

	expectTrue(task.startOnce([&]() {
		enteredPromise.set_value();
		release.wait();
		completed.store(true);
	}), "owned background task starts its worker");
	requireTrue(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
		"owned background task enters its worker");
	expectFalse(task.startOnce([]() {}), "owned background task rejects a second start");

	std::future<void> joined = std::async(std::launch::async, [&]() {
		task.join();
	});
	expectTrue(joined.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
		"join waits while the owned worker is active");
	releasePromise.set_value();
	requireTrue(joined.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
		"join completes after the owned worker exits");
	joined.get();
	expectTrue(completed.load(), "join observes completion of the owned worker");
}

void testSkinTokensCarryExplicitMode()
{
	const QStringList skinIds = {
		QStringLiteral("studio"), QStringLiteral("minimal"), QStringLiteral("soft"),
		QStringLiteral("rack"), QStringLiteral("matrix")
	};
	for (const QString& skinId : skinIds)
	{
		expectTrue(SkinThemeData::tokens(skinId, true).dark,
			QStringLiteral("%1 dark tokens carry dark=true").arg(skinId));
		expectFalse(SkinThemeData::tokens(skinId, false).dark,
			QStringLiteral("%1 light tokens carry dark=false").arg(skinId));
	}
}

void testEverySkinSheetResolvesAllThemeTokens()
{
	QDir repoRoot(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
	repoRoot.cdUp();
	repoRoot.cdUp();
	const QStringList skinIds = {
		QStringLiteral("studio"), QStringLiteral("minimal"),
		QStringLiteral("soft"), QStringLiteral("rack"), QStringLiteral("matrix")
	};
	const QRegularExpression unresolved(QStringLiteral("@[A-Z_0-9]+@"));
	for (const QString& skinId : skinIds)
	{
		for (bool dark : { false, true })
		{
			const QString resource = SkinThemeData::qssResource(skinId, dark);
			const QString sourcePath = repoRoot.filePath(
				QStringLiteral("Editor/skins/") + QFileInfo(resource).fileName());
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
