#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "Editor/ConfigFileCodec.h"
#include "Benchmark/BatchPlan.h"
#include "Editor/helpers/AnalysisWorkerRecovery.h"
#include "Editor/helpers/ConvolutionPathHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportExecutor.h"
#include "Editor/import/ImportManifest.h"
#include "Editor/import/LegacyMigrationPolicy.h"
#include "Editor/skins/SkinThemeData.h"
#include "Editor/widgets/FilterCardModel.h"
#include "Editor/widgets/FilterListModel.h"
#include "Editor/widgets/FilterListUndo.h"
#include "Editor/widgets/EditableValueText.h"
#include "Editor/widgets/cards/ChannelSelectionModel.h"
#include "Editor/widgets/cards/DeviceSelectionModel.h"
#include "Editor/widgets/cards/StageSelectionModel.h"
#include "Editor/widgets/routing/MultiConvolutionRoutingAdapter.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "Editor/widgets/routing/StudioRoutingModel.h"
#include "helpers/MemoryHelper.h"
#include "helpers/OwnedBackgroundTask.h"
#include "filters/FilterFactoryRegistry.h"
#include "helpers/StringHelper.h"
#include "helpers/Win32Resource.h"
#include "UpdateChecker/UpdateInfoFormatter.h"
#include "UpdateChecker/VelopackUpdateInfo.h"

#include "Tests/TestHarness.h"
#include "EditorLogicTestSupport.h"

// Generic assertion primitives are shared with the other suites via the
// header-only harness. The QString helpers below convert at the boundary so
// EditorLogicTests can keep its Qt-specific checks (expectPath) alongside.
// The policy argument is left off on purpose: FailurePolicy::Collect is the
// harness default now, so one broken feature block does not hide the findings
// of every block after it. The require* wrappers keep the gating checks
// aborting, and main() always reaches report().
test::Harness harness("EditorLogicTests");

std::string toStd(const QString& s)
{
	return s.toUtf8().constData();
}

QString normalized(const QString& path)
{
	return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

void fail(const QString& message)
{
	harness.fail(toStd(message));
}

void expectPath(const QString& actual, const QString& expected)
{
	harness.expectTrue(
		normalized(actual) == normalized(expected),
		toStd(QString("expected '%1', got '%2'").arg(expected, actual)));
}

void expectTrue(bool value, const QString& message)
{
	harness.expectTrue(value, toStd(message));
}

void expectFalse(bool value, const QString& message)
{
	harness.expectFalse(value, toStd(message));
}

void expectEqual(const QString& actual, const QString& expected, const QString& message)
{
	harness.expectTrue(
		actual == expected,
		toStd(QString("%1: expected '%2', got '%3'").arg(message, expected, actual)));
}

void expectEqual(int actual, int expected, const QString& message)
{
	harness.expectTrue(
		actual == expected,
		toStd(QString("%1: expected %2, got %3").arg(message).arg(expected).arg(actual)));
}

void expectEqual(const QStringList& actual, const QStringList& expected, const QString& message)
{
	harness.expectTrue(
		actual == expected,
		toStd(QString("%1: expected '%2', got '%3'").arg(message, expected.join(' '), actual.join(' '))));
}

// require counterparts of the wrappers above: same message format, but they
// abort on failure regardless of the Collect policy. Use them for checks
// whose failure would make the following lines unsafe (size checks before
// indexing, fixture-creation checks).
void requireTrue(bool value, const QString& message)
{
	harness.require(value, toStd(message));
}

// FilterListModel: the widget-free document/selection model behind
// FilterTable, so its mutation and selection logic is testable without a
// QWidget.
static void requireFilterListModelInvariants(const FilterListModel& model, const char* stage)
{
	QSet<FilterListItem*> documentItems;
	for (FilterListItem* item : model.items())
	{
		if (item == nullptr || documentItems.contains(item))
			throw std::runtime_error(std::string("FilterListModel projection invariant failed after ") + stage);
		documentItems.insert(item);
	}
	for (FilterListItem* item : model.selected())
	{
		if (!documentItems.contains(item))
			throw std::runtime_error(std::string("FilterListModel selection invariant failed after ") + stage);
	}
	if (model.focused() != nullptr && !documentItems.contains(model.focused()))
		throw std::runtime_error(std::string("FilterListModel focus invariant failed after ") + stage);
	if (model.selectionStart() != nullptr && !documentItems.contains(model.selectionStart()))
		throw std::runtime_error(std::string("FilterListModel anchor invariant failed after ") + stage);
}

static void testFilterListModel()
{
	FilterListModel model;

	// setLines resets the document, focuses/anchors the first line and starts
	// with an empty selection.
	model.setLines(QList<QString>() << "Preamp: -6 dB" << "Include: a.txt" << "Delay: 10 ms");
	expectEqual((int)model.items().size(), 3, "setLines creates one item per line");
	expectEqual(model.lines().join('\n'), "Preamp: -6 dB\nInclude: a.txt\nDelay: 10 ms", "lines() round-trips setLines");
	expectTrue(model.focused() == model.items()[0], "setLines focuses the first line");
	expectTrue(model.selectionStart() == model.items()[0], "setLines anchors the selection on the first line");
	expectTrue(model.selected().isEmpty(), "setLines starts with an empty selection");
	requireFilterListModelInvariants(model, "setLines");

	// addLine inserts before an anchor item, or appends without one.
	FilterListItem* added = model.addLine("Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71", model.items()[1]);
	expectEqual((int)model.items().indexOf(added), 1, "addLine inserts before the anchor");
	FilterListItem* appended = model.addLine("GraphicEQ: 20 -1; 1000 2");
	expectEqual((int)model.items().indexOf(appended), (int)model.items().size() - 1, "addLine without anchor appends");
	requireFilterListModelInvariants(model, "addLine");

	// Range selection math (the Shift-click/Shift-arrow logic): anchor..target
	// in either direction; a missing anchor leaves the selection untouched.
	model.setSelectionStart(model.items()[1]);
	model.selectRangeFromAnchor(model.items()[3]);
	expectEqual((int)model.selected().size(), 3, "range selection spans anchor..target");
	expectTrue(model.selected().contains(model.items()[1]) && model.selected().contains(model.items()[2])
		&& model.selected().contains(model.items()[3]),
		"range selection selects exactly the rows between anchor and target");
	model.selectRangeFromAnchor(model.items()[0]);
	expectEqual((int)model.selected().size(), 2, "a reversed range selects target..anchor");
	expectTrue(model.selected().contains(model.items()[0]) && model.selected().contains(model.items()[1]),
		"the reversed range selects rows 0..1");
	model.setSelectionStart(nullptr);
	model.selectRangeFromAnchor(model.items()[2]);
	expectEqual((int)model.selected().size(), 2, "a missing anchor leaves the selection unchanged");

	// Copy payload: selected lines in document order joined with \n, one prefs
	// map per line, regardless of the selection's insertion order.
	model.items()[0]->prefs.insert("expanded", true);
	model.items()[1]->prefs.insert("expanded", false);
	model.clearSelection();
	model.select(model.items()[1]);
	model.select(model.items()[0]);
	FilterListModel::CopyPayload payload = model.copyPayload();
	expectEqual(payload.text, model.items()[0]->text + '\n' + model.items()[1]->text,
		"copy payload joins selected lines in document order");
	expectEqual((int)payload.prefsList.size(), 2, "copy payload carries one prefs map per line");
	expectTrue(payload.prefsList[0].value("expanded").toBool() && !payload.prefsList[1].value("expanded").toBool(),
		"copy payload prefs align with their lines");
	expectEqual(model.firstSelectedIndex(), 0, "firstSelectedIndex finds the topmost selected row");

	// insertLines at a position replaces the selection with the inserted items
	// and focuses/anchors the first inserted line (the paste semantics).
	QList<FilterListItem*> inserted = model.insertLines(
		QStringList() << "Channel: L R" << "Preamp: -2 dB",
		QList<QVariantMap>() << QVariantMap({ { "expanded", true } }), 2);
	expectEqual((int)inserted.size(), 2, "insertLines returns the inserted items");
	expectEqual((int)model.items().indexOf(inserted[0]), 2, "insertLines inserts at the drop row");
	expectEqual((int)model.items().indexOf(inserted[1]), 3, "insertLines keeps the pasted order");
	expectEqual((int)model.selected().size(), 2, "insertLines replaces the selection with the inserted lines");
	expectTrue(model.selected().contains(inserted[0]) && model.selected().contains(inserted[1]),
		"insertLines selects exactly the inserted lines");
	expectTrue(model.focused() == inserted[0], "insertLines focuses the first inserted line");
	expectTrue(model.selectionStart() == inserted[0], "insertLines anchors on the first inserted line");
	expectTrue(inserted[0]->prefs.value("expanded").toBool() && inserted[1]->prefs.isEmpty(),
		"insertLines aligns prefs by index and leaves extra lines without prefs");
	requireFilterListModelInvariants(model, "insertLines");

	// deleteSelected removes exactly the selection, clears it and drops the
	// focus/anchor when they pointed at deleted rows.
	const int countBefore = (int)model.items().size();
	QList<QString> expectedRemaining;
	for (FilterListItem* item : model.items())
	{
		if (!model.selected().contains(item))
			expectedRemaining.append(item->text);
	}
	model.deleteSelected();
	expectEqual((int)model.items().size(), countBefore - 2, "deleteSelected removes exactly the selected rows");
	expectEqual(model.lines().join('\n'), expectedRemaining.join('\n'), "deleteSelected keeps the other rows in order");
	expectTrue(model.selected().isEmpty(), "deleteSelected clears the selection");
	expectTrue(model.focused() == nullptr && model.selectionStart() == nullptr,
		"deleteSelected drops focus/anchor pointing at deleted rows");
	requireFilterListModelInvariants(model, "deleteSelected");

	// selectAll selects every row.
	model.selectAll();
	expectEqual((int)model.selected().size(), (int)model.items().size(), "selectAll selects every row");

	// removeItem moves the selection, focus and anchor onto the neighbouring
	// row (the item now at the removed index).
	model.clearSelection();
	FilterListItem* victim = model.items()[1];
	model.select(victim);
	model.setFocused(victim);
	model.setSelectionStart(victim);
	expectTrue(model.removeItem(victim), "removeItem removes a known item");
	FilterListItem* replacement = model.items()[1];
	expectTrue(model.selected().contains(replacement), "removeItem re-selects the neighbouring row");
	expectTrue(model.focused() == replacement && model.selectionStart() == replacement,
		"removeItem moves focus and anchor to the neighbouring row");
	expectFalse(model.removeItem(nullptr), "removeItem rejects items outside the document");
	requireFilterListModelInvariants(model, "removeItem");

	// Batch removal has no individual replacement semantics, but must commit
	// ownership, projection and observer state together.
	FilterListModel batchModel;
	batchModel.setLines(QList<QString>() << "one" << "two" << "three");
	FilterListItem* removed = batchModel.items()[1];
	batchModel.select(removed);
	batchModel.setFocused(removed);
	batchModel.setSelectionStart(removed);
	batchModel.removeItems(QSet<FilterListItem*>() << removed);
	requireFilterListModelInvariants(batchModel, "removeItems");
	if (batchModel.lines() != (QList<QString>() << "one" << "three"))
		throw std::runtime_error("FilterListModel removeItems order invariant failed");

	// Invalid external anchors and drop rows must be normalized before the
	// owner vector is indexed; this is both a public-API guard and a regression
	// test for the former begin() - 1 undefined behavior.
	FilterListModel boundaryModel;
	boundaryModel.setLines(QList<QString>() << "middle");
	FilterListItem externalAnchor("external");
	const FilterListItem* appendedAtBoundary = boundaryModel.addLine("end", &externalAnchor);
	boundaryModel.insertLines(QStringList() << "front", {}, -100);
	boundaryModel.insertLines(QStringList() << "tail", {}, 100);
	requireFilterListModelInvariants(boundaryModel, "boundary normalization");
	if (boundaryModel.lines() != (QList<QString>() << "front" << "middle" << "end" << "tail")
		|| boundaryModel.items()[2] != appendedAtBoundary)
	{
		throw std::runtime_error("FilterListModel boundary normalization failed");
	}
}

// FilterListUndo: the widget-free undo/redo history FilterTable commits to on
// every linesChanged tick.
static void testFilterListUndo()
{
	FilterListUndo history;
	QList<QString> doc = QList<QString>() << "Preamp: -6 dB" << "Include: a.txt";

	// A fresh history has nothing to step to, and stepping anyway returns the
	// current state unchanged.
	history.reset(doc);
	expectFalse(history.canUndo(), "reset starts without undo steps");
	expectFalse(history.canRedo(), "reset starts without redo steps");
	expectEqual(history.undo().join('\n'), doc.join('\n'), "undo without steps returns the current state");
	expectEqual(history.redo().join('\n'), doc.join('\n'), "redo without steps returns the current state");

	// A structural change records one step; undo returns the prior state and
	// redo returns to the mutated state.
	QList<QString> withDelay = doc;
	withDelay.append("Delay: 10 ms");
	history.commit(withDelay);
	expectTrue(history.canUndo(), "a commit records an undo step");
	expectEqual(history.undo().join('\n'), doc.join('\n'), "undo returns the state before the commit");
	expectTrue(history.canRedo(), "undo arms redo");
	expectEqual(history.redo().join('\n'), withDelay.join('\n'), "redo returns the undone state");

	// Committing an unchanged document records nothing.
	history.commit(withDelay);
	expectTrue(history.canUndo() && !history.canRedo(), "a no-op commit records nothing but keeps history");

	// A fresh commit after undo discards the redo branch (the standard linear
	// history rule).
	history.undo();
	QList<QString> withPreamp2 = doc;
	withPreamp2.append("Preamp: -2 dB");
	history.commit(withPreamp2);
	expectFalse(history.canRedo(), "a commit after undo discards the redo branch");
	expectEqual(history.undo().join('\n'), doc.join('\n'), "the new branch undoes to the shared ancestor");
	history.redo();

	// Single-line edit runs (knob drag / typing against one row) coalesce
	// into one step per row, and a different mutation breaks the run.
	history.reset(doc);
	QList<QString> drag = doc;
	drag[0] = "Preamp: -5 dB";
	history.commit(drag);
	drag[0] = "Preamp: -4 dB";
	history.commit(drag);
	drag[0] = "Preamp: -3 dB";
	history.commit(drag);
	expectEqual(history.undo().join('\n'), doc.join('\n'), "an edit run against one line undoes as a single step");
	expectFalse(history.canUndo(), "the coalesced run is exactly one step");
	history.redo();
	QList<QString> secondRow = drag;
	secondRow[1] = "Include: b.txt";
	history.commit(secondRow);
	QList<QString> firstRowAgain = secondRow;
	firstRowAgain[0] = "Preamp: -1 dB";
	history.commit(firstRowAgain);
	expectEqual(history.undo().join('\n'), secondRow.join('\n'), "an edit of a different line starts a new step");

	// An edit run that lands back on its starting state cancels the step
	// instead of recording a no-op.
	history.reset(doc);
	QList<QString> toggled = doc;
	toggled[0] = "# Preamp: -6 dB";
	history.commit(toggled);
	history.commit(doc);
	expectFalse(history.canUndo(), "an edit run returning to the start cancels its step");

	// After an undo the same line starts a fresh step, so redo history and
	// the restored state are not silently folded together.
	history.reset(doc);
	QList<QString> editA = doc;
	editA[0] = "Preamp: -5 dB";
	history.commit(editA);
	history.undo();
	QList<QString> editB = doc;
	editB[0] = "Preamp: -4 dB";
	history.commit(editB);
	expectEqual(history.undo().join('\n'), doc.join('\n'), "an edit after undo is a fresh step, not a fold-in");
}


void requireEqual(int actual, int expected, const QString& message)
{
	harness.require(
		actual == expected,
		toStd(QString("%1: expected %2, got %3").arg(message).arg(expected).arg(actual)));
}

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

void testUpdateInfoFormatter()
{
	QJsonObject release;
	release["download-url"] = "https://example.invalid";
	QJsonArray versions;
	QJsonObject version;
	version["version"] = "<b>2.0</b>";
	version["date"] = "not-a-date";
	version["info"] = QJsonArray({ "<script>alert(1)</script>", "Fix & verify" });
	versions.append(version);
	release["versions"] = versions;

	QString newestVersion;
	QString html = UpdateInfoFormatter::releaseHtml(QJsonDocument(release), &newestVersion);
	if (newestVersion != "<b>2.0</b>")
		fail("newest version was not preserved");
	if (html.contains("<script>") || html.contains("<b>2.0</b>") || html.contains("Fix & verify"))
		fail("release notes were not HTML-escaped");
	if (!html.contains("&lt;script&gt;alert(1)&lt;/script&gt;") || !html.contains("Fix &amp; verify"))
		fail("escaped release notes are missing");
}

void testVelopackUpdateInfo()
{
	expectEqual(
		VelopackUpdateInfo::githubLatestReleaseUrl("https://github.com/115dkk/EqualizerAPO-XT/"),
		"https://api.github.com/repos/115dkk/EqualizerAPO-XT/releases/latest",
		"GitHub latest release URL");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-avx2"),
		"releases.x64-avx2.json",
		"Velopack feed file name");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-sse2"),
		"releases.x64-sse2.json",
		"Velopack SSE2 feed file name");
	expectEqual(
		VelopackUpdateInfo::feedFileName("x64-avx"),
		"releases.x64-avx.json",
		"Velopack AVX feed file name");
	expectTrue(
		VelopackUpdateInfo::isNewerVersion("v1.4.4-main.77", "1.4.3"),
		"Velopack package version was not considered newer");
	expectTrue(
		VelopackUpdateInfo::isNewerVersion("1.4.3-main.77", "1.4.3"),
		"CI package version was not considered newer than the base installed version");
	expectFalse(
		VelopackUpdateInfo::isNewerVersion("1.4.0", "1.4.3"),
		"older package version was considered newer");
}

// Builds the GitHub release fixture shared by testVelopackGitHubRelease() and
// testVelopackFeeds(): the feed conversion takes the release document as its
// fallback source, so both blocks work on the identical document.
QJsonDocument makeGitHubReleaseDoc()
{
	QJsonObject githubRelease;
	githubRelease["tag_name"] = "v1.4.4-main.77";
	githubRelease["name"] = "EqualizerAPO-XT 1.4.4-main.77";
	githubRelease["html_url"] = "https://github.com/115dkk/EqualizerAPO-XT/releases/tag/v1.4.4-main.77";
	githubRelease["published_at"] = "2026-05-22T00:00:00Z";
	githubRelease["body"] = "Fix convolution updates\nShip Velopack feeds";
	QJsonArray releaseAssets;
	releaseAssets.append(QJsonObject({
		{ "name", "releases.x64-avx2.json" },
		{ "browser_download_url", "https://example.invalid/releases.x64-avx2.json" },
	}));
	releaseAssets.append(QJsonObject({
		{ "name", "EqualizerAPO-XT-x64-avx2-1.4.4-main.77-full.nupkg" },
		{ "browser_download_url", "https://example.invalid/full.nupkg" },
	}));
	releaseAssets.append(QJsonObject({
		{ "name", "EqualizerAPO-XT-x64-avx2-Setup.exe" },
		{ "browser_download_url", "https://example.invalid/setup.exe" },
	}));
	githubRelease["assets"] = releaseAssets;
	return QJsonDocument(githubRelease);
}

void testVelopackGitHubRelease()
{
	QJsonDocument githubReleaseDoc = makeGitHubReleaseDoc();

	expectTrue(
		VelopackUpdateInfo::isGitHubRelease(githubReleaseDoc),
		"GitHub release response was not detected");
	expectEqual(
		VelopackUpdateInfo::feedAssetUrl(githubReleaseDoc, "x64-avx2"),
		"https://example.invalid/releases.x64-avx2.json",
		"Velopack feed asset URL");
	expectEqual(
		VelopackUpdateInfo::fromGitHubRelease(githubReleaseDoc, "x64-avx2", "1.4.3").object().value("download-url").toString(),
		"https://example.invalid/setup.exe",
		"GitHub release fallback setup URL");
}

void testVelopackFeeds()
{
	QJsonDocument githubReleaseDoc = makeGitHubReleaseDoc();

	QJsonArray feedAssets;
	feedAssets.append(QJsonObject({
		{ "PackageId", "EqualizerAPO-XT-x64-avx2" },
		{ "Version", "1.4.4-main.77" },
		{ "Type", "Full" },
		{ "FileName", "EqualizerAPO-XT-x64-avx2-1.4.4-main.77-full.nupkg" },
	}));
	feedAssets.append(QJsonObject({
		{ "PackageId", "EqualizerAPO-XT-x64-avx512" },
		{ "Version", "1.4.5-main.1" },
		{ "Type", "Full" },
		{ "FileName", "EqualizerAPO-XT-x64-avx512-1.4.5-main.1-full.nupkg" },
	}));
	QJsonDocument feedDoc(QJsonObject({ { "Assets", feedAssets } }));
	QJsonDocument updateDoc = VelopackUpdateInfo::fromVelopackFeed(feedDoc, githubReleaseDoc, "x64-avx2", "1.4.3");
	QJsonObject updateObj = updateDoc.object();
	expectEqual(
		updateObj.value("download-url").toString(),
		"https://example.invalid/setup.exe",
		"Velopack setup URL");
	QJsonObject velopackVersion = updateObj.value("versions").toArray().first().toObject();
	expectEqual(
		velopackVersion.value("version").toString(),
		"1.4.4-main.77",
		"Velopack update version");
	QStringList infoLines;
	for (const QJsonValue& infoValue : velopackVersion.value("info").toArray())
		infoLines.append(infoValue.toString());
	expectTrue(
		infoLines.contains("Velopack channel: x64-avx2") && infoLines.contains("Fix convolution updates"),
		"Velopack release notes were not preserved");
	expectTrue(
		VelopackUpdateInfo::fromVelopackFeed(feedDoc, githubReleaseDoc, "x64-avx2", "1.4.4-main.77").isEmpty(),
		"same Velopack version produced an update");
}

void testFilterCardDescriptors()
{
	// Every classification below runs through FilterCardModel::canonicalCommand,
	// which asks the engine's registry. The registry is filled by file-scope
	// statics in the *FilterFactory translation units, which nothing here
	// references by name - that is why this project links Common.lib whole
	// (/WHOLEARCHIVE) instead of on demand. Gate on the vocabulary so a
	// dropped factory object shows up as one clear failure instead of thirty
	// badge mismatches on cards that all fell back to raw text.
	harness.require(!FilterFactoryRegistry::knownConfigCommands().empty(),
		"knownConfigCommands() is empty; no filter factory translation unit is linked into this test binary");

	FilterCardDescriptor preamp = FilterCardModel::describeLine("Preamp: -6 dB");
	expectEqual(preamp.badge, "PRE", "preamp card badge");
	expectEqual(preamp.title, "Preamp", "preamp card title");
	expectEqual(preamp.summary, "-6 dB", "preamp card summary");
	expectTrue(preamp.enabled, "preamp card was marked disabled");

	FilterCardDescriptor disabledFilter = FilterCardModel::describeLine("# Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71");
	expectFalse(disabledFilter.enabled, "commented filter was marked enabled");
	expectEqual(disabledFilter.badge, "PK", "disabled biquad badge");
	expectEqual(disabledFilter.title, "Peaking", "disabled biquad title");
	expectEqual(disabledFilter.summary, "Fc 1000 Hz · Gain -3 dB · Q 0.71", "disabled biquad summary");

	FilterCardDescriptor lowShelfCenterFilter = FilterCardModel::describeLine("Filter: ON LSC 12 dB Fc 200 Hz Gain 3 dB");
	expectEqual(lowShelfCenterFilter.badge, "LSC", "low-shelf center badge");
	expectEqual(lowShelfCenterFilter.title, "Low-shelf", "low-shelf center title");
	expectTrue(lowShelfCenterFilter.summary.contains("Center 200 Hz") && lowShelfCenterFilter.summary.contains("Gain 3 dB") && lowShelfCenterFilter.summary.contains("Slope 12 dB/Oct"),
		QStringLiteral("low-shelf center summary missing expected fields: ") + lowShelfCenterFilter.summary);

	FilterCardDescriptor lowShelfCornerFilter = FilterCardModel::describeLine("Filter: ON LS 6 dB Fc 120 Hz Gain -2 dB");
	expectEqual(lowShelfCornerFilter.badge, "LS", "low-shelf corner badge");
	expectEqual(lowShelfCornerFilter.title, "Low-shelf", "low-shelf corner title");
	expectTrue(lowShelfCornerFilter.summary.contains("Corner 120 Hz") && lowShelfCornerFilter.summary.contains("Gain -2 dB") && lowShelfCornerFilter.summary.contains("Slope 6 dB/Oct"),
		QStringLiteral("low-shelf corner summary missing expected fields: ") + lowShelfCornerFilter.summary);

	FilterCardDescriptor pureComment = FilterCardModel::describeLine("    # purely explanatory comment");
	expectFalse(pureComment.enabled, "pure comment was marked enabled");
	expectFalse(pureComment.canToggleEnabled, "pure comment should not expose enable toggling");
	expectEqual(pureComment.badge, "#", "pure comment badge");
	expectEqual(pureComment.title, "Comment", "pure comment title");

	FilterCardDescriptor colonComment = FilterCardModel::describeLine("# TODO: explain headphone preset");
	expectEqual(colonComment.title, "Comment", "colon comment title");
	expectFalse(colonComment.canToggleEnabled, "colon comments should not become disabled commands");

	FilterCardDescriptor graphicEq = FilterCardModel::describeLine("GraphicEQ: 20 -1; 100 0; 1000 2");
	expectEqual(graphicEq.badge, "GEQ", "graphic eq badge");
	expectEqual(graphicEq.summary, "3 bands", "graphic eq band count");

	FilterCardDescriptor copy = FilterCardModel::describeLine("Copy: VL=L VR=R L=VL R=VR");
	expectEqual(copy.badge, "CPY", "copy card badge");
	expectEqual(copy.summary, "4 steps, 2 virtual", "copy card summary");
	expectTrue(copy.channelBadges.contains("L") && copy.channelBadges.contains("R"), "copy card did not expose final physical channels");

	FilterCardDescriptor channel = FilterCardModel::describeLine("Channel: L, R");
	expectEqual(channel.badge, "CH", "channel card badge");
	expectEqual(channel.summary, "L R", "channel card summary");
	expectTrue(channel.channelBadges.contains("L") && channel.channelBadges.contains("R"), "channel badges were not parsed");

	// MultiConvolution must get its own card header rather than falling through to
	// the generic TXT descriptor. Its grammar is "<output channel> <IR path>", so
	// the summary leads with the channel and ends with the file name.
	FilterCardDescriptor multiConv = FilterCardModel::describeLine("MultiConvolution: L brir.wav");
	expectEqual(multiConv.badge, "MCONV", "multiconvolution card badge");
	expectEqual(multiConv.title, "MultiConvolution", "multiconvolution card title");
	expectEqual(multiConv.type, "convolution", "multiconvolution shares the convolution row type");
	expectTrue(multiConv.summary.startsWith("L") && multiConv.summary.contains("brir.wav"),
		QStringLiteral("multiconvolution summary should show channel and file: ") + multiConv.summary);

	// A freshly inserted bare "MultiConvolution:" template (no channel/path yet)
	// still classifies as multiconvolution so the header keeps its badge instead of
	// rendering as a generic text row.
	FilterCardDescriptor multiConvBare = FilterCardModel::describeLine("MultiConvolution:");
	expectEqual(multiConvBare.badge, "MCONV", "bare multiconvolution keeps its badge");
	expectEqual(multiConvBare.type, "convolution", "bare multiconvolution keeps convolution styling");

	// The card badges carry the picker's pictograms instead
	// of English monograms. Pin the descriptor-keyed catalog: the biquad
	// prefix folding (LSC rides the low-shelf glyph, HPQ the high-pass one,
	// an unparsed BQUAD falls back to the generic peaking curve), the badge
	// split of the convolution siblings, and the empty fallback that keeps
	// unmapped raw text lines on their monogram.
	expectEqual(FilterCardModel::badgeIconResource("biquad", "PK"), ":/icons/modern/eq-peaking.svg", "peaking badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("biquad", "LSC"), ":/icons/modern/eq-lowshelf.svg", "LSC badge pictogram folds onto low-shelf");
	expectEqual(FilterCardModel::badgeIconResource("biquad", "HPQ"), ":/icons/modern/eq-highpass.svg", "HPQ badge pictogram folds onto high-pass");
	expectEqual(FilterCardModel::badgeIconResource("biquad", "BQUAD"), ":/icons/modern/eq-peaking.svg", "unparsed biquad falls back to the peaking curve");
	expectEqual(FilterCardModel::badgeIconResource("convolution", "CONV"), ":/icons/modern/waveform.svg", "convolution badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("convolution", "MCONV"), ":/icons/modern/multi-convolution.svg", "multiconvolution badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("device", "DEV"), ":/icons/modern/device-speaker.svg", "device badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("comment", "#"), ":/icons/modern/comment-bubble.svg", "comment badge pictogram");
	expectTrue(FilterCardModel::badgeIconResource("text", "TXT").isEmpty(), "raw text lines keep their monogram fallback");
	expectEqual(FilterCardModel::commandIconResource("MultiConvolution"), ":/icons/modern/multi-convolution.svg",
		"picker command vocabulary shares the multiconvolution pictogram");
	expectEqual(FilterCardModel::commandIconResource("Filter", "ON HP Fc 120 Hz"), ":/icons/modern/eq-highpass.svg",
		"picker command vocabulary shares the biquad curve split");

	// The programmatic vocabulary is modelled. The
	// If family shares one card type with per-branch badges, Eval gets its own
	// type, the condition/expression is the summary, and a parameterless line
	// does not echo itself twice ("ENDIF  EndIf:"). A bare note line keeps the
	// whole line as its summary.
	FilterCardDescriptor ifLine = FilterCardModel::describeLine("If: inputChannelCount == 2");
	expectEqual(ifLine.type, "if", "If line carries the if card type");
	expectEqual(ifLine.badge, "IF", "If line badge");
	expectEqual(ifLine.title, "If", "If line title");
	expectEqual(ifLine.summary, "inputChannelCount == 2", "If line summary carries the condition");

	FilterCardDescriptor elseIfLine = FilterCardModel::describeLine("ElseIf: sampleRate > 96000");
	expectEqual(elseIfLine.type, "if", "ElseIf shares the if card type");
	expectEqual(elseIfLine.badge, "ELIF", "ElseIf badge tells the branch kind");
	expectEqual(elseIfLine.summary, "sampleRate > 96000", "ElseIf summary carries the condition");

	FilterCardDescriptor elseLine = FilterCardModel::describeLine("Else:");
	expectEqual(elseLine.type, "if", "Else shares the if card type");
	expectEqual(elseLine.badge, "ELSE", "Else badge");
	expectTrue(elseLine.summary.isEmpty(),
		QStringLiteral("parameterless Else must not echo the raw line as summary: ") + elseLine.summary);

	FilterCardDescriptor endIfLine = FilterCardModel::describeLine("EndIf:");
	expectEqual(endIfLine.type, "if", "EndIf shares the if card type");
	expectEqual(endIfLine.badge, "ENDIF", "EndIf badge");
	expectEqual(endIfLine.title, "End if", "EndIf line title");
	expectTrue(endIfLine.summary.isEmpty(),
		QStringLiteral("parameterless EndIf must not echo the raw line as summary: ") + endIfLine.summary);

	FilterCardDescriptor evalLine = FilterCardModel::describeLine("Eval: gain = -3 + 1.5");
	expectEqual(evalLine.type, "eval", "Eval line carries the eval card type");
	expectEqual(evalLine.badge, "EVAL", "Eval badge");
	expectEqual(evalLine.summary, "gain = -3 + 1.5", "Eval summary carries the expression");

	// Dynamic-commands finishing pass: If/Eval wear real pictograms (the
	// decision diamond and the fx formula mark) instead of the monogram.
	expectEqual(FilterCardModel::badgeIconResource("if", "IF"), ":/icons/modern/logic-if.svg", "if badge pictogram");
	expectEqual(FilterCardModel::badgeIconResource("eval", "EVAL"), ":/icons/modern/logic-eval.svg", "eval badge pictogram");

	FilterCardDescriptor bareText = FilterCardModel::describeLine("plain note line without a command");
	expectEqual(bareText.title, "Text", "bare text line title");
	expectEqual(bareText.summary, "plain note line without a command", "bare text line keeps its content as summary");

	// Classification follows the engine, casing and all. A lower-case key is
	// prose in Equalizer APO 1.4.2 and stays prose here, because a card would
	// let one knob turn rewrite an inert note into a line that processes audio.
	FilterCardDescriptor lowerCasePreamp = FilterCardModel::describeLine("preamp: -6 dB");
	expectEqual(lowerCasePreamp.type, "text", "a lower-case \"preamp:\" key is prose to the engine, so it gets no card");
	FilterCardDescriptor lowerCaseCopyNote = FilterCardModel::describeLine("copy: remember to re-measure the room");
	expectEqual(lowerCaseCopyNote.type, "text", "the 1.4.2 \"copy: a note to self\" line must not become a routing card");

	// A numbered key is the Filter family's own grammar (REW and Dirac write
	// it), so those keep their card. No other factory accepts a trailing token:
	// "Channel 2:" is a line the engine recognizes but never runs, and its card
	// would rewrite the key to "Channel" on the first edit.
	FilterCardDescriptor numberedFilter = FilterCardModel::describeLine("Filter 1: ON PK Fc 1000 Hz Gain -3 dB Q 0.71");
	expectEqual(numberedFilter.type, "biquad", "a numbered Filter line keeps the biquad card");
	expectEqual(numberedFilter.badge, "PK", "a numbered Filter line keeps its parsed badge");
	FilterCardDescriptor numberedChannel = FilterCardModel::describeLine("Channel 2: L R");
	expectEqual(numberedChannel.type, "text", "only the Filter family takes a trailing token, so \"Channel 2:\" stays raw text");
	FilterCardDescriptor numberedCopy = FilterCardModel::describeLine("Copy 2: L=R");
	expectEqual(numberedCopy.type, "text", "\"Copy 2:\" must not open the routing view that would rewrite it as \"Copy:\"");

	// Dynamic-value contract: hasInlineExpressions must follow the engine's
	// InlineExpression lexer exactly, because it decides which lines may not
	// open a parsing editor (a knob turn would serialize the expression
	// away). Escaped backticks are literal, an empty `` still counts as an
	// expression (the engine reports its evaluation error), and the content
	// of an unterminated trailing expression is dropped like the engine
	// drops it.
	expectTrue(FilterCardModel::hasInlineExpressions("`bass + 3` dB"), "backtick expression detected");
	expectTrue(FilterCardModel::hasInlineExpressions("prefix `x` suffix"), "embedded expression detected");
	expectFalse(FilterCardModel::hasInlineExpressions("-3 dB"), "plain number is not dynamic");
	expectFalse(FilterCardModel::hasInlineExpressions("\\` literal backtick"), "escaped backtick is literal");
	expectTrue(FilterCardModel::hasInlineExpressions("``"), "empty expression still counts");
	expectFalse(FilterCardModel::hasInlineExpressions("`unterminated"), "unterminated trailing expression is dropped");
}

void testFilterCardDepths()
{
	QVector<int> depths = FilterCardModel::calculateDepths(QList<QString>({
		"Channel: L R",
		"Preamp: -6 dB",
		"Include: nested.txt",
		"Delay: 10 ms",
		"# Channel: R",
		"Preamp: -4 dB",
		"Channel: ALL",
		"Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71"
	}));
	requireEqual(depths.size(), 8, "channel depth count");
	expectEqual(depths[0], 0, "channel command depth");
	expectEqual(depths[1], 1, "scoped preamp depth");
	expectEqual(depths[2], 1, "include depth");
	expectEqual(depths[3], 1, "include should preserve channel depth");
	expectEqual(depths[4], 1, "commented channel must not reset depth");
	expectEqual(depths[5], 1, "post-comment channel depth");
	expectEqual(depths[6], 0, "channel all depth");
	expectEqual(depths[7], 0, "post channel-all depth");

	// If opens a nestable scope that EndIf closes.
	// The indent axis puts members one level in while ElseIf/Else/EndIf sit at
	// their block head's level; the logic axis counts the scope a row lives in,
	// where branch/tail rows count their own scope so a painted rail can pass
	// through them and terminate on EndIf. Commented-out If lines are comments
	// to the engine and must not move either axis; a stray EndIf clamps at 0.
	QVector<FilterCardRowScope> scopes = FilterCardModel::calculateScopes(QList<QString>({
		"If: inputChannelCount == 2",             // 0: head, indent 0, logic 0
		"Preamp: -6 dB",                          // 1: member, indent 1, logic 1
		"If: sampleRate > 96000",                 // 2: nested head, indent 1, logic 1
		"Delay: 10 ms",                           // 3: nested member, indent 2, logic 2
		"ElseIf: sampleRate > 48000",             // 4: branch at head level, indent 1, logic 2
		"Else:",                                  // 5: branch at head level, indent 1, logic 2
		"# If: never == 1",                       // 6: commented If moves nothing, indent 2, logic 2
		"EndIf:",                                 // 7: closes nested scope, indent 1, logic 2
		"Eval: gain = -3",                        // 8: back in outer scope, indent 1, logic 1
		"EndIf:",                                 // 9: closes outer scope, indent 0, logic 1
		"Preamp: 0 dB",                           // 10: outside, indent 0, logic 0
		"EndIf:"                                  // 11: stray EndIf clamps, indent 0, logic 0
	}));
	requireEqual(scopes.size(), 12, "if scope count");
	expectEqual(scopes[0].indent, 0, "if head indent");
	expectEqual(scopes[0].logic, 0, "if head logic depth counts only outer scopes");
	expectEqual(scopes[1].indent, 1, "member indent");
	expectEqual(scopes[1].logic, 1, "member logic depth");
	expectEqual(scopes[2].indent, 1, "nested head indent");
	expectEqual(scopes[2].logic, 1, "nested head logic depth");
	expectEqual(scopes[3].indent, 2, "nested member indent");
	expectEqual(scopes[3].logic, 2, "nested member logic depth");
	expectEqual(scopes[4].indent, 1, "elseif sits at its head's indent");
	expectEqual(scopes[4].logic, 2, "elseif counts its own scope");
	expectEqual(scopes[5].indent, 1, "else sits at its head's indent");
	expectEqual(scopes[5].logic, 2, "else counts its own scope");
	expectEqual(scopes[6].indent, 2, "commented if is an ordinary member");
	expectEqual(scopes[6].logic, 2, "commented if moves no scope");
	expectEqual(scopes[7].indent, 1, "endif sits at its head's indent");
	expectEqual(scopes[7].logic, 2, "endif counts the scope it closes");
	expectEqual(scopes[8].indent, 1, "outer member indent after nested block");
	expectEqual(scopes[8].logic, 1, "outer member logic depth after nested block");
	expectEqual(scopes[9].indent, 0, "outer endif indent");
	expectEqual(scopes[9].logic, 1, "outer endif counts the scope it closes");
	expectEqual(scopes[10].indent, 0, "post-block indent");
	expectEqual(scopes[10].logic, 0, "post-block logic depth");
	expectEqual(scopes[11].indent, 0, "stray endif clamps indent at zero");
	expectEqual(scopes[11].logic, 0, "stray endif clamps logic depth at zero");

	// Channel grouping and If nesting are independent axes that add up on the
	// indent; a Channel row inside an If block indents with the block and
	// resets only the channel axis.
	QVector<FilterCardRowScope> mixed = FilterCardModel::calculateScopes(QList<QString>({
		"Channel: L R",                           // 0: indent 0
		"If: sampleRate == 48000",                // 1: indent 1 (channel scope), logic 0
		"Preamp: -2 dB",                          // 2: indent 2, logic 1
		"Channel: ALL",                           // 3: channel reset inside block, indent 1, logic 1
		"Preamp: -1 dB",                          // 4: indent 1, logic 1
		"EndIf:"                                  // 5: indent 0? (channel now 0) logic 1
	}));
	requireEqual(mixed.size(), 6, "mixed scope count");
	expectEqual(mixed[1].indent, 1, "if head inherits channel indent");
	expectEqual(mixed[2].indent, 2, "member stacks channel and if indent");
	expectEqual(mixed[3].indent, 1, "channel row indents with the enclosing block");
	expectEqual(mixed[3].logic, 1, "channel row logic depth inside block");
	expectEqual(mixed[4].indent, 1, "post channel-all member keeps if indent");
	expectEqual(mixed[5].logic, 1, "endif closes the scope in mixed nesting");
	// The scope also names the active selection: rows under "Channel: L R"
	// carry {L, R} until Channel: ALL clears it. The selection survives If
	// nesting, and a commented-out Channel line changes nothing.
	expectEqual(mixed[1].channels, QStringList({ "L", "R" }), "if head carries the channel selection");
	expectEqual(mixed[2].channels, QStringList({ "L", "R" }), "member carries the channel selection");
	expectEqual(mixed[4].channels, QStringList(), "channel-all clears the selection");
	expectEqual(mixed[5].channels, QStringList(), "endif after channel-all carries no selection");

	QVector<FilterCardRowScope> channelScopes = FilterCardModel::calculateScopes(QList<QString>({
		"Channel: SL SR",                         // 0: selects SL SR
		"Preamp: -3 dB",                          // 1: under SL SR
		"# Channel: L",                           // 2: comment, selection unchanged
		"Delay: 1 ms",                            // 3: still under SL SR
		"Channel: ALL",                           // 4: back to all channels
		"Preamp: 0 dB"                            // 5: no selection
	}));
	requireEqual(channelScopes.size(), 6, "channel selection scope count");
	expectEqual(channelScopes[1].channels, QStringList({ "SL", "SR" }), "member under channel selection");
	expectEqual(channelScopes[3].channels, QStringList({ "SL", "SR" }), "commented channel keeps the selection");
	expectEqual(channelScopes[5].channels, QStringList(), "channel-all resets the selection");
}

void testFilterCardBuildPlans()
{
	const QList<QString> lines({
		"Channel: L R",
		"If: sampleRate == 48000",
		"Copy: L=R*`gain`",
		"EndIf:",
		"Channel: ALL"
	});
	const QVector<FilterCardBuildPlan> plans = FilterCardModel::prepareRows(lines);
	const QVector<FilterCardRowScope> scopes = FilterCardModel::calculateScopes(lines);
	requireEqual(plans.size(), lines.size(), "card build-plan count");
	requireEqual(scopes.size(), lines.size(), "card build-plan scope count");

	for (int i = 0; i < plans.size(); ++i)
	{
		expectEqual(plans[i].descriptor.depth, scopes[i].indent,
			QString("build plan %1 descriptor indent").arg(i));
		expectEqual(plans[i].descriptor.logicDepth, scopes[i].logic,
			QString("build plan %1 descriptor logic depth").arg(i));
		expectEqual(plans[i].scope.indent, scopes[i].indent,
			QString("build plan %1 scope indent").arg(i));
		expectEqual(plans[i].scope.logic, scopes[i].logic,
			QString("build plan %1 scope logic depth").arg(i));
	}

	expectEqual(plans[0].descriptor.parameters, "L R", "build plan preserves parsed parameters");
	expectTrue(plans[2].descriptor.dynamicLine, "build plan carries inline-expression state");
	expectEqual(plans[2].descriptor.type, "copy", "build plan carries the prepared card type");
	expectEqual(plans[2].descriptor.scopeChannels, QStringList({ "L", "R" }),
		"build plan carries the enclosing channel selection");
	expectEqual(plans[1].descriptor.scopeChannels, QStringList({ "L", "R" }),
		"if head carries the enclosing channel selection");
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

void testChannelSelectionModel()
{
	// ChannelSelectionModel serialization identity: for equivalent
	// selections the in-place chip editor must write the same bytes the
	// legacy multi-select dialog produced (standard positions in the
	// dialog's checkbox order, then non-standard device channels, then
	// custom names).
	const std::vector<std::wstring> stereo = { L"L", L"R" };
	const std::vector<std::wstring> surround51 = { L"L", L"R", L"C", L"LFE", L"RL", L"RR" };
	const std::vector<std::wstring> surround71 = { L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR" };

	ChannelSelectionModel model;
	model.load("L R C", surround51);
	expectEqual(model.serialize(), "C L R", "5.1 selection must serialize in dialog order");

	model.load("R L", stereo);
	expectEqual(model.serialize(), "L R", "written order canonicalizes like the dialog");

	// Position numbers resolve against the device order (engine
	// semantics, ChannelHelper::getChannelIndex), and are written back
	// as names like the dialog did.
	model.load("2", stereo);
	expectEqual(model.serialize(), "R", "numeric selector resolves in device order");

	// Historical aliases follow the engine: SUB -> LFE, SL <-> RL.
	model.load("SUB", surround51);
	expectEqual(model.serialize(), "LFE", "SUB alias selects the LFE chip");
	model.load("SL", surround51);
	expectEqual(model.serialize(), "RL", "SL on a back-channel device selects RL");

	model.load("SR SL LFE", surround71);
	expectEqual(model.serialize(), "SL SR LFE", "7.1 selection serializes in dialog order");

	// ALL wins over individual selections, exactly like the dialog.
	model.load("ALL L", surround51);
	expectTrue(model.allSelected(), "ALL token sets the all-channels state");
	expectEqual(model.serialize(), "ALL", "ALL serializes alone");

	// Custom/virtual channels keep their written order after the device
	// chips, matching the dialog's list section.
	model.load("VSL L VSR", stereo);
	expectEqual(model.serialize(), "L VSL VSR", "custom names follow device channels");
	model.toggle("R");
	expectEqual(model.serialize(), "L R VSL VSR", "toggling keeps canonical order");
	model.toggle("L");
	expectEqual(model.serialize(), "R VSL VSR", "deselecting removes the token");

	expectFalse(model.addCustom("  "), "blank custom name is rejected");
	expectFalse(model.addCustom("A B"), "multi-token custom name is rejected");
	expectTrue(model.addCustom(" vrr "), "custom name is trimmed and accepted");
	expectEqual(model.serialize(), "R VSL VSR VRR", "added custom name serializes upper-cased");

	// addCustom resolves aliases against the device set too: SUB selects
	// the LFE chip instead of duplicating it as a custom name.
	model.load("", surround51);
	expectTrue(model.addCustom("sub"), "SUB through addCustom is accepted");
	expectEqual(model.serialize(), "LFE", "SUB resolves to the device's LFE chip");
}

void testDeviceSelectionModel()
{
	// DeviceSelectionModel serialization identity: for equivalent device
	// selections the in-place chip editor must write the same bytes the
	// legacy change-button dialog produced - "all", or each selected
	// device's device string joined with "; " in list order (output
	// devices first, then input). Matching runs through the shared
	// DeviceCommand codec, the same one the engine uses, so a chip is
	// pre-selected exactly when the engine would match that device.
	auto dev = [](const QString& deviceString, const QString& name, bool installed, bool isInput) {
		DeviceEntry e;
		e.deviceString = deviceString;
		e.name = name;
		e.installed = installed;
		e.isInput = isInput;
		return e;
	};
	const QString devSpeakers = "Speakers Realtek HD Audio {0.0.0.00000000}.{aaaaaaaa-1111-2222-3333-444444444444}";
	const QString devHeadphones = "Headphones Realtek HD Audio {0.0.0.00000000}.{bbbbbbbb-1111-2222-3333-444444444444}";
	const QString devDigital = "Digital Output Realtek HD Audio {0.0.0.00000000}.{cccccccc-1111-2222-3333-444444444444}";
	const QString devMic = "Microphone Realtek HD Audio {0.0.1.00000000}.{dddddddd-1111-2222-3333-444444444444}";
	const QList<DeviceEntry> devices = {
		dev(devSpeakers, "Speakers", true, false),
		dev(devHeadphones, "Headphones", true, false),
		dev(devDigital, "Digital Output", false, false),
		dev(devMic, "Microphone", true, true),
	};

	DeviceSelectionModel model;

	// The literal lowercase "all" line is the all-devices state, like the
	// dialog's "All devices" choice: it round-trips to "all" and marks no
	// individual chip selected.
	model.load("all", devices);
	expectTrue(model.allSelected(), "literal 'all' sets the all-devices state");
	expectEqual(model.serialize(), "all", "all-devices serializes back as 'all'");

	// An empty parameter is not the all state and selects nothing.
	model.load("", devices);
	expectFalse(model.allSelected(), "empty parameter is not the all state");
	expectEqual(model.serialize(), "", "no selection serializes empty");

	// A full device-string pattern pre-selects exactly that endpoint and
	// round-trips byte-for-byte, GUID included.
	model.load(devSpeakers, devices);
	expectFalse(model.allSelected(), "a specific device is not the all state");
	expectEqual(model.serialize(), devSpeakers, "single device round-trips verbatim");

	// A bare word matches as a case-insensitive substring (DeviceCommand
	// semantics) and is rewritten to the matched device's full string.
	model.load("headphones", devices);
	expectEqual(model.serialize(), devHeadphones, "word pattern selects and canonicalizes to the device string");

	// Multiple patterns, written input-first, serialize in list order
	// (output devices first, then input) joined with "; ".
	model.load(devMic + "; " + devSpeakers, devices);
	expectEqual(model.serialize(), devSpeakers + "; " + devMic, "multiple devices serialize in list order");

	// toggle() flips one chip and keeps canonical list order.
	model.load(devSpeakers, devices);
	model.toggle(devHeadphones);
	expectEqual(model.serialize(), devSpeakers + "; " + devHeadphones, "toggling on adds a chip in list order");
	model.toggle(devSpeakers);
	expectEqual(model.serialize(), devHeadphones, "toggling off removes the token");

	// "All devices" wins over individual selections, like the dialog.
	model.load(devSpeakers, devices);
	model.setAllSelected(true);
	expectEqual(model.serialize(), "all", "All overrides individual selections");
}

void testMultiConvolutionRoutingAdapter()
{
	// MultiConvolutionRoutingAdapter: mappings <-> the routing views'
	// Assignment type must round-trip, because the card serializes the
	// edited view back into the config line. IR channels ride as decimal
	// summand channels carrying their factor.
	using Mapping = MultiConvolutionCommand::Mapping;
	using IrRef = MultiConvolutionCommand::IrChannelRef;

	const std::vector<Mapping> brir = {{L"L", {0, 1}}, {L"R", {2, 3}}};
	std::vector<Assignment> assignments = MultiConvolutionRoutingAdapter::toAssignments(brir, 4);
	requireEqual((int)assignments.size(), 2, "two mappings become two assignments");
	expectTrue(assignments.size() == 2
		&& assignments[0].targetChannel == L"L" && assignments[0].sourceSum.size() == 2
		&& assignments[0].sourceSum[0].channel == L"0" && assignments[0].sourceSum[1].channel == L"1"
		&& assignments[0].sourceSum[0].factor == 1.0 && !assignments[0].sourceSum[0].isDecibel,
		"IR channels become decimal summands at unity factor");

	std::vector<Mapping> roundTrip = MultiConvolutionRoutingAdapter::toMappings(assignments);
	expectTrue(roundTrip.size() == 2
		&& roundTrip[0].targetChannel == L"L" && roundTrip[0].irChannels == std::vector<IrRef>({0, 1})
		&& roundTrip[1].targetChannel == L"R" && roundTrip[1].irChannels == std::vector<IrRef>({2, 3}),
		"assignments convert back to the same mappings");

	// A factor set in the view survives the trip to mappings and back, so the
	// per-summand gain/phase editing the Copy views offer works here too.
	std::vector<Assignment> withFactor = assignments;
	withFactor[0].sourceSum[0].factor = -0.5;
	std::vector<Mapping> factored = MultiConvolutionRoutingAdapter::toMappings(withFactor);
	expectTrue(factored.size() == 2
		&& factored[0].irChannels == std::vector<IrRef>({IrRef(0, -0.5), IrRef(1)}),
		"summand factors ride into the mappings");
	std::vector<Assignment> factorBack = MultiConvolutionRoutingAdapter::toAssignments(factored, 4);
	expectTrue(factorBack.size() == 2 && factorBack[0].sourceSum.size() == 2
		&& factorBack[0].sourceSum[0].factor == -0.5 && !factorBack[0].sourceSum[0].isDecibel
		&& factorBack[0].sourceSum[1].factor == 1.0,
		"summand factors ride back into the assignments");

	// The simple form expands to every file channel for display, and to
	// nothing when the channel count is unknown (callers must not offer
	// editing then).
	std::vector<Assignment> expanded = MultiConvolutionRoutingAdapter::toAssignments({{L"Wet", {}}}, 3);
	expectTrue(expanded.size() == 1 && expanded[0].sourceSum.size() == 3
		&& expanded[0].sourceSum[2].channel == L"2",
		"the simple form expands to every file channel");
	std::vector<Assignment> unknown = MultiConvolutionRoutingAdapter::toAssignments({{L"Wet", {}}}, 0);
	expectTrue(unknown.size() == 1 && unknown[0].sourceSum.empty(),
		"an unknown channel count expands to nothing");

	// Seeded placeholder rows (empty sums) and non-numeric summands are
	// dropped on the way back, like Copy's serializer skips empty rows.
	std::vector<Assignment> edited = assignments;
	Assignment seeded;
	seeded.targetChannel = L"C";
	edited.push_back(seeded);
	Assignment::Summand bogus;
	bogus.factor = 1.0;
	bogus.channel = L"VSL";
	edited[0].sourceSum.push_back(bogus);
	std::vector<Mapping> cleaned = MultiConvolutionRoutingAdapter::toMappings(edited);
	expectTrue(cleaned.size() == 2 && cleaned[0].irChannels == std::vector<IrRef>({0, 1}),
		"placeholder rows and non-numeric summands are dropped");

	// Source ports: "0".."N-1" from the file, then any referenced index
	// beyond the file so stale connections stay visible and removable.
	QStringList ports = MultiConvolutionRoutingAdapter::sourcePorts(2, {{L"L", {0, 7}}});
	expectEqual(ports.join(','), QString("0,1,7"), "ports are the file channels plus stale references");
	QStringList portsNoFile = MultiConvolutionRoutingAdapter::sourcePorts(0, {{L"L", {2, 1}}});
	expectEqual(portsNoFile.join(','), QString("1,2"), "without a file only referenced indices appear, sorted");
}

void testStageSelectionModel()
{
	// StageSelectionModel serialization identity: known stages come back in
	// the legacy checkbox GUI's canonical order (pre-mix, post-mix,
	// capture), case-insensitively parsed through the shared StageCommand
	// codec; unlike the legacy GUI, tokens outside the vocabulary survive
	// an edit in their written order.
	StageSelectionModel model;

	model.load("post-mix pre-mix");
	expectTrue(model.isSelected("pre-mix") && model.isSelected("post-mix"), "both written stages are selected");
	expectFalse(model.isSelected("capture"), "capture stays unselected");
	expectEqual(model.serialize(), "pre-mix post-mix", "known stages serialize in canonical order");

	model.load("Pre-Mix CAPTURE");
	expectEqual(model.serialize(), "pre-mix capture", "selectors are case-insensitive and lower-cased");

	model.load("pre-mix render foo");
	expectEqual(model.unknownTokens().join(' '), "render foo", "unknown tokens are reported");
	expectEqual(model.serialize(), "pre-mix render foo", "unknown tokens survive after the known stages");
	model.setSelected("pre-mix", false);
	model.setSelected("capture", true);
	expectEqual(model.serialize(), "capture render foo", "toggles keep the unknown tokens");

	model.load("");
	expectEqual(model.serialize(), "", "an empty selection serializes empty (matches no stage)");
	model.setSelected("capture", true);
	expectEqual(model.serialize(), "capture", "a single selection writes just its token");
}

void testStudioRoutingModel()
{
	// StudioRoutingModel: the Light Trace view's working model must seed
	// and resolve exactly like the legacy CopyFilterGUIScene (channel rows,
	// LFE/SUB alias, 1-based numeric positions, the constant input port)
	// while preserving load order, so an edit-free round trip emits the
	// assignments in the order they were written.
	auto formatted = [](const std::vector<Assignment>& list) {
		QString out;
		for (const Assignment& a : list)
		{
			if (!out.isEmpty())
				out += ' ';
			out += QString::fromStdWString(a.targetChannel) + '=';
			bool first = true;
			for (const Assignment::Summand& s : a.sourceSum)
			{
				if (!first)
					out += '+';
				first = false;
				out += QString::number(s.factor) + (s.isDecibel ? "db" : "") + '*'
					+ (s.channel.empty() ? QStringLiteral("<const>") : QString::fromStdWString(s.channel));
			}
		}
		return out;
	};
	auto summand = [](double factor, const wchar_t* channel, bool isDecibel = false) {
		Assignment::Summand s;
		s.factor = factor;
		s.isDecibel = isDecibel;
		s.channel = channel;
		return s;
	};

	const std::vector<std::wstring> surround = { L"L", L"R", L"C", L"LFE" };
	StudioRoutingModel::PortConfig copyMode;

	// Written order survives, SUB canonicalizes to the LFE chip, the
	// unknown target VC becomes a new output chip.
	std::vector<Assignment> loaded(2);
	loaded[0].targetChannel = L"VC";
	loaded[0].sourceSum = { summand(0.5, L"L"), summand(0.5, L"R") };
	loaded[1].targetChannel = L"C";
	loaded[1].sourceSum = { summand(1.0, L"SUB") };

	StudioRoutingModel model;
	model.load(loaded, surround, copyMode);
	expectEqual(model.inputPorts().join(','), "L,R,C,LFE,", "inputs are the channels plus the constant port");
	expectTrue(model.constInput(model.inputPorts().size() - 1), "the last input is the constant port");
	expectEqual(model.outputPorts().join(','), "L,R,C,LFE,VC", "the unknown target joins the output row");
	expectEqual(formatted(model.assignments()), "VC=0.5*L+0.5*R C=1*LFE",
		"round trip keeps written order and canonicalizes SUB to LFE");

	// 1-based numeric positions resolve like the engine.
	std::vector<Assignment> numeric(1);
	numeric[0].targetChannel = L"3";
	numeric[0].sourceSum = { summand(1.0, L"1") };
	model.load(numeric, { L"L", L"R", L"C" }, copyMode);
	expectEqual(formatted(model.assignments()), "C=1*L", "numeric positions canonicalize to channel names");

	// Edit operations: virtual output, unity trace, factor grammar
	// (',' reads as '.', "db" suffix any case, empty removes).
	model.load({}, { L"L", L"R" }, copyMode);
	expectEqual(formatted(model.assignments()), "", "seeded chips without traces emit nothing");
	const int vx = model.addOutput("VX");
	expectTrue(vx >= 0 && model.outputPorts().last() == "VX", "a new output chip is appended");
	expectEqual(model.addOutput("L"), 0, "an existing name reuses its chip");
	model.addTrace(0, vx);
	expectEqual(formatted(model.assignments()), "VX=1*L", "a drag connects at unity gain");
	model.setFactorText(0, "0,5");
	expectEqual(formatted(model.assignments()), "VX=0.5*L", "a comma factor reads as a decimal point");
	model.setFactorText(0, "-6 dB");
	expectEqual(formatted(model.assignments()), "VX=-6db*L", "a db suffix sets decibel mode");
	model.setFactorText(0, "garbage");
	expectEqual(formatted(model.assignments()), "VX=-6db*L", "unparsable text leaves the trace unchanged");
	model.setFactorText(0, "");
	expectEqual(formatted(model.assignments()), "", "an empty commit removes the trace");

	// The constant port connects at factor 0.0 with no channel.
	model.load({}, { L"L", L"R" }, copyMode);
	model.addTrace(2, 0);
	expectEqual(formatted(model.assignments()), "L=0*<const>", "the constant port writes a value summand");

	// Fixed-source mode (MultiConvolution): the top row is exactly the
	// given port list, no constant port, factors locked to unity.
	StudioRoutingModel::PortConfig fixedMode;
	fixedMode.fixedSources = QStringList() << "0" << "1" << "2" << "3";
	fixedMode.allowFactors = false;
	std::vector<Assignment> mapped(1);
	mapped[0].targetChannel = L"L";
	mapped[0].sourceSum = { summand(1.0, L"0"), summand(1.0, L"1") };
	model.load(mapped, { L"L", L"R" }, fixedMode);
	expectEqual(model.inputPorts().join(','), "0,1,2,3", "fixed sources are the whole top row");
	expectFalse(model.constInput(model.inputPorts().size() - 1), "no constant port in fixed mode");
	model.setFactorText(0, "0.5");
	model.addTrace(2, 1);
	expectEqual(formatted(model.assignments()), "L=1*0+1*1 R=1*2", "fixed mode keeps every factor at unity");

	// removeChannel: the named channel leaves both rows with every touching
	// trace, and the surviving trace indices stay consistent.
	std::vector<Assignment> removable(2);
	removable[0].targetChannel = L"VC";
	removable[0].sourceSum = { summand(0.5, L"L"), summand(0.5, L"R") };
	removable[1].targetChannel = L"R";
	removable[1].sourceSum = { summand(1.0, L"VC"), summand(1.0, L"L") };
	model.load(removable, { L"L", L"R" }, copyMode);
	expectTrue(model.removeChannel("vc"), "removing a connected channel reports a change (case-insensitive)");
	expectEqual(formatted(model.assignments()), "R=1*L", "the channel's own traces and its summand uses are gone");
	expectFalse(model.outputPorts().contains("VC"), "the output chip is gone");
	expectFalse(model.inputPorts().contains("VC"), "the input chip is gone");
	expectTrue(model.constInput(model.inputPorts().size() - 1), "the constant port survives the index shift");
	expectFalse(model.removeChannel("XX"), "removing an unknown channel reports no change");
}

void testRoutingFold()
{
	// RoutingFold: the Copy views' channel fold. A collapsed view lists only
	// the channels the command involves; the seeded rest waits behind the
	// reveal control. (README task: Copy GUI area growth on multi-channel
	// devices.)
	auto summand = [](double factor, const wchar_t* channel) {
		Assignment::Summand s;
		s.factor = factor;
		s.isDecibel = false;
		s.channel = channel;
		return s;
	};
	const std::vector<std::wstring> surround =
	{ L"L", L"R", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR" };

	// "Copy: VC=0.5*L+0.5*R R=L" over 7.1: two listed rows, six folded.
	std::vector<Assignment> parsed(2);
	parsed[0].targetChannel = L"VC";
	parsed[0].sourceSum = { summand(0.5, L"L"), summand(0.5, L"R") };
	parsed[1].targetChannel = L"R";
	parsed[1].sourceSum = { summand(1.0, L"L") };
	std::vector<Assignment> seeded = parsed;
	for (const wchar_t* name : { L"L", L"C", L"LFE", L"RL", L"RR", L"SL", L"SR" })
	{
		Assignment a;
		a.targetChannel = name;
		seeded.push_back(a);
	}

	RoutingFold::Fold collapsed = RoutingFold::fold(seeded, surround,
		RoutingFold::referencedTargets(parsed), false);
	expectEqual(collapsed.visibleRows.size(), 2, "collapsed: only the referenced targets are listed");
	expectTrue(collapsed.visibleRows.contains(0) && collapsed.visibleRows.contains(1),
		"collapsed: the listed rows are the parsed ones");
	expectEqual(collapsed.hiddenChannels, 7, "collapsed: the seeded rest is counted as hidden");
	expectEqual(collapsed.inputs.join(','), "L,R,VC", "collapsed: inputs are the referenced sums plus pins");

	RoutingFold::Fold expanded = RoutingFold::fold(seeded, surround,
		RoutingFold::referencedTargets(parsed), true);
	expectEqual(expanded.visibleRows.size(), (int)seeded.size(), "expanded: every seeded row is listed");
	expectEqual(expanded.hiddenChannels, 0, "expanded: nothing is hidden");
	expectEqual(expanded.inputs.join(','), "L,R,VC,C,LFE,RL,RR,SL,SR",
		"expanded: referenced inputs first, then the device layout");

	// An empty Copy shows the first two device channels as representatives.
	std::vector<Assignment> emptySeeded;
	for (const std::wstring& name : surround)
	{
		Assignment a;
		a.targetChannel = name;
		emptySeeded.push_back(a);
	}
	RoutingFold::Fold reps = RoutingFold::fold(emptySeeded, surround, QStringList(), false);
	expectEqual(reps.visibleRows.size(), 2, "empty: two representative rows");
	expectTrue(reps.visibleRows.contains(0) && reps.visibleRows.contains(1),
		"empty: the representatives are the first two device channels");
	expectEqual(reps.hiddenChannels, 6, "empty: the rest is hidden");
	expectEqual(reps.inputs.join(','), "L,R", "empty: representative input columns");

	// A pinned (user-added) virtual channel stays listed with an empty sum
	// and is offered as an input column - and it must not chase the
	// representatives away, or there would be nothing to route it from.
	std::vector<Assignment> pinnedSeeded = emptySeeded;
	Assignment vs;
	vs.targetChannel = L"VS";
	pinnedSeeded.push_back(vs);
	RoutingFold::Fold pinned = RoutingFold::fold(pinnedSeeded, surround,
		QStringList() << "VS", false);
	expectEqual(pinned.visibleRows.size(), 3, "pinned: the added channel joins the representatives");
	expectTrue(pinned.visibleRows.contains((int)pinnedSeeded.size() - 1),
		"pinned: the appended row is listed");
	expectEqual(pinned.inputs.join(','), "VS,L,R",
		"pinned: the added channel and the representatives are routable from");

	// Name validation: the Copy grammar's operators and pure numbers are
	// rejected, plain alphanumeric names pass.
	expectTrue(RoutingFold::isValidChannelName("VS"), "a plain name is valid");
	expectTrue(RoutingFold::isValidChannelName("XL2"), "letters and digits are valid");
	expectFalse(RoutingFold::isValidChannelName(""), "empty is rejected");
	expectFalse(RoutingFold::isValidChannelName("a b"), "whitespace is rejected");
	expectFalse(RoutingFold::isValidChannelName("a=b"), "the assignment operator is rejected");
	expectFalse(RoutingFold::isValidChannelName("a+b"), "the summand operator is rejected");
	expectFalse(RoutingFold::isValidChannelName("0.5"), "a factor-shaped token is rejected");
	expectFalse(RoutingFold::isValidChannelName("2"), "a positional number is rejected");
	expectFalse(RoutingFold::isValidChannelName("ABCDEFGHIJKLMNOPQ"), "over-long names are rejected");

	// removeChannel: the channel leaves as a target and as a summand; the
	// return value says whether the serialized line changed.
	std::vector<Assignment> removable = seeded;
	expectTrue(RoutingFold::removeChannel(removable, "vc"),
		"removing a referenced channel reports a change (case-insensitive)");
	expectEqual((int)removable.size(), (int)seeded.size() - 1, "the target row is gone");
	for (const Assignment& a : removable)
		for (const Assignment::Summand& s : a.sourceSum)
			expectTrue(QString::fromStdWString(s.channel).compare("VC", Qt::CaseInsensitive) != 0,
				"no summand references the removed channel");
	std::vector<Assignment> seedOnly = emptySeeded;
	expectFalse(RoutingFold::removeChannel(seedOnly, "SR"),
		"folding away a pure seed row is not a serialized change");
	expectEqual((int)seedOnly.size(), (int)emptySeeded.size() - 1, "the seed row itself is still removed");
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





int main(int argc, char** argv)
{
	try
	{
		QCoreApplication app(argc, argv);

		testConvolutionPathHelper();
		testAnalysisWorkerRecovery();
		testUpdateInfoFormatter();
		testVelopackUpdateInfo();
		testVelopackGitHubRelease();
		testVelopackFeeds();
		testFilterCardDescriptors();
		testFilterCardDepths();
		testFilterCardBuildPlans();
		testConfigImport();
		testLegacyMigrationScanAndPolicy();
		testChannelSelectionModel();
		testDeviceSelectionModel();
		testMultiConvolutionRoutingAdapter();
		testStageSelectionModel();
		testStudioRoutingModel();
		testRoutingFold();
		testAnalysisResponseBinArithmetic();
		testAnalysisResponseEmptyAndLatency();
		testConfigFileCodec();
		testConfigFileCodecPreservesExistingFileWhenAtomicReplaceFails();
		testConfigFileCodecRejectsPartialRead();
		testMemoryHelperConstructReleasesStorageWhenConstructorThrows();
		testOwnedBackgroundTaskJoinsAndStartsOnlyOnce();
		testSkinTokensCarryExplicitMode();
		testEverySkinSheetResolvesAllThemeTokens();
		testEditableValueTextUsesDisplayedDecimalFormatFirst();
		testBenchmarkBatchPlanUsesOnlyComparableFullBatches();
		testFileReferenceControllerOwnsPathState();
		testFilterListModel();
		testFilterListUndo();

		harness.report();
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Unhandled exception escaped a test: " << error.what() << '\n';
	}
	catch (...)
	{
		std::cerr << "A non-standard exception escaped a test.\n";
	}

	return EXIT_FAILURE;
}
