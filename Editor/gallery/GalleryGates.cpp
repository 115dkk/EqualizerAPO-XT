/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The offscreen robustness gates over a gallery-built FilterTable:
	--skin-switch-test, --card-move-test and --card-selection-test.
*/

#include "SkinGallery.h"
#include "Editor/gallery/GallerySupport.h"
#include "Editor/gallery/GalleryStallWatch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include "Editor/FilterTable.h"
#include "Editor/FilterTemplate.h"
#include "Editor/SkinManager.h"
#include "Editor/diagnostics/ToolbarPixelProbe.h"
#include "Editor/guis/CopyFilterGUI.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/SkinThemeData.h"
#include "Editor/skins/Skins.h"
#include "Editor/skins/shared/SkinFileIcons.h"
#include "Editor/widgets/AudioKnob.h"
#include "Editor/widgets/KnobTravel.h"
#include "Editor/widgets/CommandRowFrame.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/FilterInsertSeam.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/MainToolbarKit.h"
#include "Editor/widgets/TitleBar.h"
#include "Editor/widgets/UpdateToast.h"
#include "Editor/widgets/cards/VSTPluginSession.h"

using namespace SkinGalleryDetail;

namespace
{
// When the stall watch starts sampling: at the gate's budget, or at `factor`
// times the median of the operations this run has timed so far, whichever is
// lower. A fast runner then still reports an operation that took twice its
// usual time, which the budget alone would let pass unexplained.
int stallThreshold(QList<qint64> previous, int budgetMs, double factor)
{
	if (previous.size() < 3)
		return budgetMs;
	std::sort(previous.begin(), previous.end());
	const qint64 median = previous[previous.size() / 2];
	return int(std::min<qint64>(budgetMs, qint64(factor * double(median))));
}

// The skin switch gate in its phases (audit #467 TD-38), called by
// runSwitchTest in the original order.

// Skins::all() leaves out (and logs) a roster id with no ISkin, so this
// gate would switch through fewer skins than the preferences offer while
// the missing one paints as Studio.
bool switchRosterComplete()
{
	const int rosterSize = int(SkinThemeData::ids().size());
	const int implemented = int(Skins::all().size());
	if (implemented != rosterSize)
	{
		qWarning("SkinSwitchTest: only %d of the %d roster skins have an ISkin implementation (Skins.cpp)",
			implemented, rosterSize);
		return false;
	}
	return true;
}

bool seamHasKeyboardParity(FilterInsertSeam& accessibilitySeam, int& seamActivations)
{
	int* const activations = &seamActivations;
	QObject::connect(&accessibilitySeam, &FilterInsertSeam::activated,
		[activations]() { (*activations)++; });
	QKeyEvent activateSeam(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
	QApplication::sendEvent(&accessibilitySeam, &activateSeam);
	if (accessibilitySeam.focusPolicy() != Qt::StrongFocus || seamActivations != 1
		|| accessibilitySeam.accessibleName().isEmpty())
	{
		qWarning("SkinSwitchTest: insertion seam lacks keyboard accessibility parity");
		return false;
	}
	return true;
}

bool toastTimerBehaves(UpdateToast& timerProbe)
{
	timerProbe.showMessage(QStringLiteral("temporary"), 15000);
	const QTimer* const autoHideTimer = timerProbe.findChild<QTimer*>(QStringLiteral("UpdateToastAutoHide"));
	if (autoHideTimer == nullptr || !autoHideTimer->isActive())
	{
		qWarning("SkinSwitchTest: update toast does not own a restartable auto-hide timer");
		return false;
	}
	timerProbe.showMessage(QStringLiteral("persistent"), 0);
	if (autoHideTimer->isActive())
	{
		qWarning("SkinSwitchTest: persistent update toast kept a stale auto-hide timer");
		return false;
	}
	return true;
}

// Everything in the action train must stay alive and laid out: an action
// reported invisible, an item widget QToolBarLayout hid (overflow into the
// extension popup counts - the real toolbar never overflows at 1024px), or
// a collapsed toolbar all reproduce the "toolbar is gone" field state.
int checkSwitchToolbar(const QMainWindow& probeWindow, QToolBar* probeToolBar, const QString& switchName)
{
	int problems = 0;
	QLabel* formatBadge = probeToolBar->findChild<QLabel*>(
		QStringLiteral("DeviceFormatBadge"), Qt::FindDirectChildrenOnly);
	if (formatBadge == nullptr
		|| formatBadge->property("severity").toString() != QLatin1String("warning"))
	{
		qWarning("SkinSwitchTest: %s: representative DeviceFormatBadge is missing",
			qPrintable(switchName));
		problems++;
	}
	if (!probeToolBar->isVisibleTo(&probeWindow))
	{
		qWarning("SkinSwitchTest: %s: main toolbar widget is hidden", qPrintable(switchName));
		problems++;
	}
	if (probeToolBar->height() < 16)
	{
		qWarning("SkinSwitchTest: %s: main toolbar collapsed to %dpx",
			qPrintable(switchName), probeToolBar->height());
		problems++;
	}
	for (QAction* action : probeToolBar->actions())
	{
		QWidget* item = probeToolBar->widgetForAction(action);
		// State-driven items legitimately hide; every other item belongs
		// to the structural health contract.
		if (item != nullptr
			&& MainToolbarKit::visibilityIsDataObjectNames().contains(item->objectName()))
			continue;
		const QString label = item != nullptr && !item->objectName().isEmpty()
			? item->objectName() : action->objectName();
		if (!action->isVisible())
		{
			qWarning("SkinSwitchTest: %s: toolbar action %s turned invisible",
				qPrintable(switchName), qPrintable(label));
			problems++;
		}
		else if (item != nullptr && item->isHidden())
		{
			qWarning("SkinSwitchTest: %s: toolbar item %s was hidden by the layout (item hint %dpx, bar %dpx wide, bar hint %dpx)",
				qPrintable(switchName), qPrintable(label),
				item->sizeHint().width(), probeToolBar->width(),
				probeToolBar->sizeHint().width());
			problems++;
		}
	}
	// A rendered toolbar is never one flat colour: buttons, combos and labels
	// cover much of it. Matching the corner pixel almost everywhere means the
	// controls were not painted, even when every logical probe stayed healthy.
	if (probeToolBar->isVisible()
		&& ToolbarPixelProbe::renderIsBlank(probeToolBar->grab().toImage().convertToFormat(QImage::Format_RGB32)))
	{
		qWarning("SkinSwitchTest: %s: toolbar rendered blank (controls not painted)",
			qPrintable(switchName));
		problems++;
	}
	return problems;
}

// Generous ceiling: a healthy switch is well under a second offscreen,
// the regression class this guards against cost multiple seconds per
// switch, and CI runners are slow and variable. Overridable for local
// tuning.
void readSwitchBudget(int& limitMs, int& warningMs)
{
	bool limitOk = false;
	limitMs = qEnvironmentVariableIntValue("EAPO_SWITCH_LIMIT_MS", &limitOk);
	if (!limitOk || limitMs <= 0)
		limitMs = 8000;
	bool warningOk = false;
	warningMs = qEnvironmentVariableIntValue("EAPO_SWITCH_WARN_MS", &warningOk);
	if (!warningOk || warningMs <= 0 || warningMs >= limitMs)
		warningMs = 0;
}

int checkPaintOnlyChrome(const QScrollArea& scrollArea, const QString& objectName)
{
	const QList<QWidget*> widgets = scrollArea.findChildren<QWidget*>(objectName);
	if (widgets.isEmpty())
	{
		qWarning("SkinSwitchTest: paint-only chrome %s is missing", qPrintable(objectName));
		return 1;
	}
	for (const QWidget* widget : widgets)
	{
		if (!widget->testAttribute(Qt::WA_NoSystemBackground))
		{
			qWarning("SkinSwitchTest: paint-only chrome %s accepts a framework background",
				qPrintable(objectName));
			return 1;
		}
	}
	return 0;
}

int checkLegacyCopyScene(FilterTable* table)
{
	int failures = 0;
	// LegacyRows still uses CopyFilterGUI. Its QGraphicsView does not own
	// the scene, so the GUI must parent it explicitly; allWidgets() cannot
	// detect this leak because QGraphicsScene is not a QWidget.
	CopyFilterGUI* legacyCopy = new CopyFilterGUI({}, table);
	QGraphicsView* graphicsView = legacyCopy->findChild<QGraphicsView*>();
	QPointer<QGraphicsScene> scene = graphicsView != nullptr ? graphicsView->scene() : nullptr;
	if (scene.isNull())
	{
		qWarning("SkinSwitchTest: legacy CopyFilterGUI scene was not created");
		failures++;
	}
	delete legacyCopy;
	if (!scene.isNull())
	{
		qWarning("SkinSwitchTest: deleting CopyFilterGUI did not delete its scene");
		delete scene.data();
		failures++;
	}
	return failures;
}

// Caption ink check. tintedIcon paints every covered pixel
// in the ink colour, so the strongest-coverage pixel must
// sit on the active skin's text colour. Tolerance, not
// equality: the thin minimize stroke has no fully opaque
// pixel at 14 px and unpremultiply rounding shifts channels
// by a few counts - a stale light/dark ink is off by ~100+.
int checkCaptionInk(const QToolButton* captionButton, const QString& name)
{
	const QColor ink(SkinManager::instance()->tokens().text);
	const QImage glyph = captionButton->icon()
		.pixmap(QSize(14, 14)).toImage().convertToFormat(QImage::Format_ARGB32);
	int bestAlpha = 0;
	QColor bestPixel;
	for (int y = 0; y < glyph.height(); y++)
		for (int x = 0; x < glyph.width(); x++)
		{
			const QColor pixel = glyph.pixelColor(x, y);
			if (pixel.alpha() > bestAlpha)
			{
				bestAlpha = pixel.alpha();
				bestPixel = pixel;
			}
		}
	const bool inkMatched = bestAlpha > 0
		&& qAbs(bestPixel.red() - ink.red()) <= 8
		&& qAbs(bestPixel.green() - ink.green()) <= 8
		&& qAbs(bestPixel.blue() - ink.blue()) <= 8;
	if (!inkMatched)
	{
		qWarning("SkinSwitchTest: caption icon did not follow the switch to %s (ink %s, glyph %s a%d)",
			qPrintable(name), qPrintable(ink.name()), qPrintable(bestPixel.name()), bestAlpha);
		return 1;
	}
	return 0;
}

// The skinned file dialog's icon provider must serve the
// drive glyph for both drive spellings: the real root
// ("C:/") and the slash-less shell name ("C:") that
// QFileSystemModel stores drive nodes under and rebuilds
// node icons from on setIconProvider. The bare spelling
// is neither isRoot() nor a file, which dressed every
// sidebar drive with the folder pictogram in the field.
int checkDriveGlyph(const QString& skinId)
{
	QFileDialog probeDialog;
	probeDialog.setOption(QFileDialog::DontUseNativeDialog);
	SkinManager::instance()->styleFileDialog(&probeDialog);
	if (SkinFileIconProvider* provider
		= dynamic_cast<SkinFileIconProvider*>(probeDialog.iconProvider()))
	{
		const qint64 driveKey
			= provider->icon(QAbstractFileIconProvider::Drive).cacheKey();
		const qint64 folderKey
			= provider->icon(QAbstractFileIconProvider::Folder).cacheKey();
		if (driveKey == folderKey
			|| provider->icon(QFileInfo(QStringLiteral("C:"))).cacheKey() != driveKey
			|| provider->icon(QFileInfo(QStringLiteral("C:/"))).cacheKey() != driveKey)
		{
			qWarning("SkinSwitchTest: %s file-dialog provider does not classify a drive root as the drive glyph",
				qPrintable(skinId));
			return 1;
		}
	}
	return 0;
}

// What the switches of one gate run share: the table and chrome they
// probe, the budget, and the running failure count and timings.
struct SwitchRun
{
	SwitchRun(FilterTable* filterTable, const QScrollArea& area, const QToolButton* caption,
		const QMainWindow& window, QToolBar* toolBar, int limit, int warning)
		: table(filterTable), scrollArea(area), captionButton(caption), probeWindow(window),
		probeToolBar(toolBar), limitMs(limit), warningMs(warning)
	{
	}

	FilterTable* table;
	const QScrollArea& scrollArea;
	const QToolButton* captionButton;
	const QMainWindow& probeWindow;
	QToolBar* probeToolBar;
	const int limitMs;
	const int warningMs;
	int failures = 0;
	qint64 worstMs = 0;
	QString worstName;
	QList<qint64> switchTimes;
};

void runOneSwitch(SwitchRun& run, ISkin* skin, bool dark, int round)
{
	FilterTable* table = run.table;
	int& failures = run.failures;
	const int limitMs = run.limitMs;
	const int warningMs = run.warningMs;
	const QString name = QStringLiteral("%1/%2").arg(skin->id(), dark ? QStringLiteral("dark") : QStringLiteral("light"));

	QElapsedTimer timer;
	timer.start();
	qint64 clearMs = 0;
	qint64 applyMs = 0;
	qint64 rebuildMs = 0;
	{
		// Prints the GUI thread's stacks when the switch runs long.
		const GalleryStallWatch watch("SkinSwitchTest",
			name + QStringLiteral(" round %1").arg(round),
			stallThreshold(run.switchTimes, warningMs > 0 ? warningMs : limitMs / 2, 1.5));
		// MainWindow::skinSelected's exact live sequence: tear the
		// rows down BEFORE the global stylesheet swap (which also
		// re-derives the palette), rebuild after.
		table->clearRows();
		clearMs = timer.restart();
		SkinManager::instance()->applySkin(skin->id(), dark);
		applyMs = timer.restart();
		table->updateGuis();
		VSTPluginSession::waitForPendingLoads(30000);
		QApplication::processEvents();
		// The live editor returns to the event loop between switches,
		// which is when deleteLater victims (combo popup containers,
		// editor internals) actually die; a bare processEvents() does
		// not deliver DeferredDelete, and without this the harness
		// accumulates a dead generation per switch that the real app
		// never keeps.
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QApplication::processEvents();
		rebuildMs = timer.elapsed();
	}
	const qint64 elapsed = clearMs + applyMs + rebuildMs;
	run.switchTimes.append(elapsed);

	if (SkinManager::instance()->currentSkinId() != skin->id())
	{
		qWarning("SkinSwitchTest: switch to %s resolved to %s",
			qPrintable(name), qPrintable(SkinManager::instance()->currentSkinId()));
		failures++;
	}
	if (skin->id() == QLatin1String("flat"))
		failures += checkPaintOnlyChrome(run.scrollArea, QStringLiteral("FlatReferenceTile"));
	failures += checkCaptionInk(run.captionButton, name);
	failures += checkSwitchToolbar(run.probeWindow, run.probeToolBar, name);
	if (round == 1 && dark)
		failures += checkDriveGlyph(skin->id());
	if (elapsed > limitMs)
	{
		qWarning("SkinSwitchTest: switch to %s took %lld ms (limit %d ms)",
			qPrintable(name), static_cast<long long>(elapsed), limitMs);
		failures++;
	}
	else if (warningMs > 0 && elapsed > warningMs)
	{
		qWarning("SkinSwitchTest: switch to %s took %lld ms (warning %d ms; hard limit %d ms)",
			qPrintable(name), static_cast<long long>(elapsed), warningMs, limitMs);
	}
	if (elapsed > run.worstMs)
	{
		run.worstMs = elapsed;
		run.worstName = name;
	}
	qWarning("SkinSwitchTest: round %d %s: %lld ms (clear %lld, apply %lld, rebuild %lld, widgets %lld)",
		round, qPrintable(name), static_cast<long long>(elapsed),
		static_cast<long long>(clearMs), static_cast<long long>(applyMs), static_cast<long long>(rebuildMs),
		static_cast<long long>(QApplication::allWidgets().size()));
}

// Diagnostic: class histogram of the surviving widgets, top entries.
int reportWidgetCensus()
{
	int failures = 0;
	QHash<QByteArray, int> histogram;
	for (QWidget* widget : QApplication::allWidgets())
		histogram[widget->metaObject()->className()]++;
	const int legacyCopyWidgets = histogram.value(QByteArrayLiteral("CopyFilterGUI"));
	if (legacyCopyWidgets != 0)
	{
		qWarning("SkinSwitchTest: modern cards retained %d CopyFilterGUI widgets", legacyCopyWidgets);
		failures++;
	}
	QList<QPair<int, QByteArray>> ranked;
	for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it)
		ranked.append({ it.value(), it.key() });
	std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	for (int i = 0; i < qMin(10, int(ranked.size())); i++)
		qWarning("SkinSwitchTest: widget census %s x%d", ranked[i].second.constData(), ranked[i].first);

	// Top-level population: a growing count here is how the leaked
	// parentless CopyFilterGUI generation was originally spotted.
	int windows = 0;
	for (QWidget* widget : QApplication::allWidgets())
		if (widget->isWindow())
			windows++;
	qWarning("SkinSwitchTest: %d top-level widgets", windows);
	return failures;
}
}

namespace SkinGallery
{
int runSwitchTest(const QStringList& arguments)
{
	Q_UNUSED(arguments);

	qWarning("SkinSwitchTest: starting");
	// What else ran on the machine during the gate, printed before the verdict.
	const GalleryLoadSummary load("SkinSwitchTest");

	if (!switchRosterComplete())
		return 1;

	FilterInsertSeam accessibilitySeam;
	int seamActivations = 0;
	if (!seamHasKeyboardParity(accessibilitySeam, seamActivations))
		return 1;

	QWidget toastHost;
	UpdateToast timerProbe(&toastHost);
	if (!toastTimerBehaves(timerProbe))
		return 1;

	const GalleryFixture fixture("SkinSwitchTest");
	if (!fixture.isValid())
		return 2;

	// A config heavy enough for the historical failure mode: the field crash
	// and the seconds-per-switch regression both needed a loaded document,
	// not the empty tree the gallery switches under. Six copies of the
	// representative rows exercise every card type at over a hundred rows.
	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	buildRows(scrollArea, fixture.configPath(), lines);
	FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
	if (table == nullptr)
	{
		qWarning("SkinSwitchTest: table construction failed");
		return 1;
	}
	// Gallery tables deliberately have no MainWindow. This supported host mode
	// must keep navigation callbacks harmless.
	table->openConfig(QString());

	// A live TitleBar rides along: its caption glyphs are tinted icons, not
	// QSS, so a switch path that forgets to re-dress them leaves them in the
	// previous skin's ink (the field report: black glyphs on the dark strip
	// after a light->dark toggle). Dark/light alternate every switch below,
	// so a stale icon is guaranteed to mismatch the active ink.
	QWidget titleHost;
	TitleBar titleBar(&titleHost);
	QToolButton* captionButton = titleBar.findChild<QToolButton*>(QStringLiteral("TitleBarMin"));
	if (captionButton == nullptr)
	{
		qWarning("SkinSwitchTest: TitleBarMin caption button not found");
		return 1;
	}

	// The main toolbar rides along in a real QMainWindow top area, wired the
	// way MainWindow wires it (skinChanged -> styleMainToolbar). The gallery
	// builds a fresh replica per skin, so per-skin chrome state that survives
	// on ONE long-lived toolbar across revisits (rack's plate + ear-spacer
	// actions, matrix's board layers) is only exercised here - the field
	// report was the whole action train vanishing after returning to an
	// already-visited skin.
	QMainWindow probeWindow;
	// Wide enough that the full action train genuinely fits in every skin's
	// paddings: a hidden item then always means the layout lost the room, not
	// that the room was honestly missing.
	probeWindow.resize(1600, 768);
	QToolBar* probeToolBar = buildToolbarReplica(nullptr);
	probeWindow.addToolBar(Qt::TopToolBarArea, probeToolBar);
	probeWindow.setCentralWidget(new QWidget(&probeWindow));
	probeWindow.show();
	QObject::connect(SkinManager::instance(), &SkinManager::skinChanged, probeToolBar,
		[probeToolBar](const SkinTokens&) { SkinManager::instance()->styleMainToolbar(probeToolBar); });

	int limitMs = 0;
	int warningMs = 0;
	readSwitchBudget(limitMs, warningMs);

	SwitchRun run(table, scrollArea, captionButton, probeWindow, probeToolBar, limitMs, warningMs);
	QApplication::processEvents();
	run.failures += checkSwitchToolbar(probeWindow, probeToolBar, QStringLiteral("baseline (before any switch)"));
	run.failures += checkLegacyCopyScene(table);
	const int rounds = 3;
	for (int round = 1; round <= rounds; round++)
	{
		forEachSkinMode([&run, round](ISkin* skin, bool dark) {
			runOneSwitch(run, skin, dark, round);
		});
	}

	run.failures += reportWidgetCensus();

	load.report();
	qWarning("SkinSwitchTest: %d switches over %d rows, worst %lld ms (%s), warning %d ms, limit %d ms, failures %d",
		rounds * int(Skins::all().size()) * 2, int(lines.size()), static_cast<long long>(run.worstMs),
		qPrintable(run.worstName), warningMs, limitMs, run.failures);

	return GalleryFixture::finish(run.failures == 0 ? 0 : 1);
}

int runCardMoveTest(const QStringList& arguments)
{
	Q_UNUSED(arguments);

	qWarning("CardMoveTest: starting");
	// What else ran on the machine during the gate, printed before the verdict.
	const GalleryLoadSummary load("CardMoveTest");

	const GalleryFixture fixture("CardMoveTest");
	if (!fixture.isValid())
		return 2;

	// The same 100+ row document as the switch test: the field lag needs a
	// loaded document, and six copies of the representative rows exercise
	// every card type.
	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	buildRows(scrollArea, fixture.configPath(), lines);
	FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
	if (table == nullptr)
	{
		qWarning("CardMoveTest: table construction failed");
		return 1;
	}
	// Gallery tables deliberately have no MainWindow; navigation callbacks
	// must stay harmless in this supported host mode.
	table->openConfig(QString());

	// Budget contract like the switch test: the limit fails the gate, the
	// warning only logs. The field regression class this measures cost 5-6 s
	// per move on a fast desktop, so even the generous default limit would
	// catch a return to a full rebuild on a slow runner.
	bool limitOk = false;
	int limitMs = qEnvironmentVariableIntValue("EAPO_MOVE_LIMIT_MS", &limitOk);
	if (!limitOk || limitMs <= 0)
		limitMs = 20000;
	bool warningOk = false;
	int warningMs = qEnvironmentVariableIntValue("EAPO_MOVE_WARN_MS", &warningOk);
	if (!warningOk || warningMs <= 0 || warningMs >= limitMs)
		warningMs = 0;

	int failures = 0;
	int moves = 0;
	qint64 worstMs = 0;
	QString worstName;
	QList<qint64> moveTimes;
	forEachSkinMode([&](ISkin* skin, bool dark) {
		const QString name = QStringLiteral("%1/%2").arg(skin->id(),
			dark ? QStringLiteral("dark") : QStringLiteral("light"));

		// Fresh rows under this skin, in the live switch order (tear down
		// before the stylesheet swap, rebuild after).
		table->clearRows();
		SkinManager::instance()->applySkin(skin->id(), dark);
		table->updateGuis();
		VSTPluginSession::waitForPendingLoads(30000);
		QApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QApplication::processEvents();

		const int rowCount = int(table->documentItems().count());
		if (rowCount != lines.size())
		{
			qWarning("CardMoveTest: %s expected %lld rows, found %d",
				qPrintable(name), static_cast<long long>(lines.size()), rowCount);
			failures++;
			return;
		}

		// One card moved down one row, then back up: two timed commits of
		// the drag-move path per scene, and the document leaves the scene
		// in its original order.
		const int from = rowCount / 2;
		for (int pass = 0; pass < 2; pass++)
		{
			const QList<QString> before = table->getLines();
			const int sourceRow = pass == 0 ? from : from + 1;
			const int targetRow = pass == 0 ? from + 1 : from;
			const int dropRow = pass == 0 ? from + 2 : from;
			FilterTable::Item* moved = table->documentItems().at(sourceRow);

			QElapsedTimer timer;
			timer.start();
			qint64 elapsed = 0;
			{
				// Prints the GUI thread's stacks when the move runs long.
				const GalleryStallWatch watch("CardMoveTest",
					name + QStringLiteral(" move %1").arg(pass + 1),
					stallThreshold(moveTimes, warningMs > 0 ? warningMs : limitMs / 2, 2.0));
				table->moveRows({ moved }, dropRow);
				QApplication::processEvents();
				elapsed = timer.elapsed();
			}
			moves++;
			moveTimes.append(elapsed);

			QList<QString> expected = before;
			expected.move(sourceRow, targetRow);
			if (table->getLines() != expected)
			{
				qWarning("CardMoveTest: %s move %d produced a wrong document order",
					qPrintable(name), pass + 1);
				failures++;
			}
			// The moved line must stay the (only) selection, at its new
			// row - by position, not pointer, so both the copy-splice and
			// the item-preserving implementations of the move pass.
			const QSet<FilterTable::Item*>& selectedItems = table->getSelectedItems();
			if (selectedItems.size() != 1
				|| table->documentItems().indexOf(*selectedItems.cbegin()) != targetRow)
			{
				qWarning("CardMoveTest: %s move %d did not leave the moved row selected",
					qPrintable(name), pass + 1);
				failures++;
			}
			const int rowWidgets = int(table->findChildren<FilterCardRow*>(
				QString(), Qt::FindDirectChildrenOnly).count());
			if (rowWidgets != rowCount)
			{
				qWarning("CardMoveTest: %s move %d left %d row widgets for %d rows",
					qPrintable(name), pass + 1, rowWidgets, rowCount);
				failures++;
			}

			if (elapsed > limitMs)
			{
				qWarning("CardMoveTest: %s move %d took %lld ms (limit %d ms)",
					qPrintable(name), pass + 1, static_cast<long long>(elapsed), limitMs);
				failures++;
			}
			else if (warningMs > 0 && elapsed > warningMs)
			{
				qWarning("CardMoveTest: %s move %d took %lld ms (warning %d ms; hard limit %d ms)",
					qPrintable(name), pass + 1, static_cast<long long>(elapsed), warningMs, limitMs);
			}
			if (elapsed > worstMs)
			{
				worstMs = elapsed;
				worstName = name;
			}
			qWarning("CardMoveTest: %s move %d: %lld ms (rows %d, widgets %lld)",
				qPrintable(name), pass + 1, static_cast<long long>(elapsed), rowCount,
				static_cast<long long>(QApplication::allWidgets().size()));
		}
	});

	load.report();
	qWarning("CardMoveTest: %d moves over %lld rows, worst %lld ms (%s), warning %d ms, limit %d ms, failures %d",
		moves, static_cast<long long>(lines.size()), static_cast<long long>(worstMs),
		qPrintable(worstName), warningMs, limitMs, failures);

	// Scroll-position contract for the row edits a user makes from a card's
	// own header (+ / - / text edit) and for the full rebuild behind every
	// other structural change. Field report: adding or removing one line
	// flashed the whole list and threw the view back to the top, so on a
	// long document every edit cost a scroll back down. The scrolled view
	// must stay where it was, within the height of the row that changed.
	{
		scrollArea.show();
		QApplication::processEvents();
		QScrollBar* bar = scrollArea.verticalScrollBar();
		const int rowCount = int(table->documentItems().count());
		const int middle = rowCount / 2;
		const int target = bar->maximum() / 2;
		const auto settle = [&bar]() {
			QApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();
			return bar->value();
		};
		const auto check = [&](const char* what, int before, int after) {
			// One card's height is the most a splice above the viewport may
			// legitimately shift the content by.
			const int tolerance = 160;
			if (std::abs(after - before) > tolerance)
			{
				qWarning("CardMoveTest: scroll after %s jumped %d -> %d (max %d)", what, before, after, bar->maximum());
				failures++;
			}
			else
			{
				qWarning("CardMoveTest: scroll after %s held %d -> %d (max %d)", what, before, after, bar->maximum());
			}
		};

		bar->setValue(target);
		int before = settle();
		if (before <= 0)
		{
			qWarning("CardMoveTest: the document does not scroll (max %d); scroll contract not exercised", bar->maximum());
			failures++;
		}
		else
		{
			FilterTable::Item* anchor = table->documentItems().at(middle);
			FilterTable::Item* inserted = table->insertLine(QStringLiteral("Preamp: -1 dB"), anchor);
			check("header insert", before, settle());
			before = bar->value();
			table->removeLine(inserted);
			check("header remove", before, settle());
			before = bar->value();
			table->updateGuis();
			VSTPluginSession::waitForPendingLoads(30000);
			check("full rebuild", before, settle());
			// The live skin switch tears down first and rebuilds after the
			// stylesheet swap (MainWindow::skinSelected); the position must
			// survive that gap too.
			before = bar->value();
			table->clearRows();
			SkinManager::instance()->applySkin(Skins::all().first()->id(), false);
			table->updateGuis();
			VSTPluginSession::waitForPendingLoads(30000);
			check("skin switch", before, settle());
		}
	}

	return GalleryFixture::finish(failures == 0 ? 0 : 1);
}

}

namespace
{
// The card selection gate in its phases (audit #467 TD-38): one scene per
// skin and mode, each running the checks below in the original order.
void cardClick(QWidget* target, const QPoint& localPos)
{
	const QPoint globalPos = target->mapToGlobal(localPos);
	QMouseEvent press(QEvent::MouseButtonPress, QPointF(localPos), QPointF(globalPos),
		Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(target, &press);
	QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(globalPos),
		Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(target, &release);
	QApplication::processEvents();
}

void cardPressKey(QWidget* target, int key)
{
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QApplication::sendEvent(target, &press);
	QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(target, &release);
	QApplication::processEvents();
}

// What QApplication does before it delivers a real (spontaneous) press:
// the focus goes to the nearest enabled widget under the press, the
// target or an ancestor, that takes click focus, unless one on the way
// already holds it. sendEvent() skips that step, and the #456 double
// toggle of the raw-line button lived exactly there.
void cardClickWithFocus(QWidget* target, const QPoint& localPos)
{
	QPoint pos = localPos;
	for (QWidget* widget = target; widget != nullptr; widget = widget->parentWidget())
	{
		if (widget->isEnabled() && widget->rect().contains(pos)
			&& (widget->focusPolicy() & Qt::ClickFocus) == Qt::ClickFocus)
		{
			widget->setFocus(Qt::MouseFocusReason);
			break;
		}
		if (widget->isWindow() || widget->hasFocus())
			break;
		pos += widget->pos();
	}
	cardClick(target, localPos);
}

void cardSendMouse(QWidget* target, QEvent::Type type, const QPointF& localPos,
	Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
	QMouseEvent event(type, localPos, target->mapToGlobal(localPos), button, buttons, modifiers);
	QApplication::sendEvent(target, &event);
}

struct CardSelectionRun
{
	CardSelectionRun(const QString& config, const QDir& output, bool capture)
		: configPath(config), outputDir(output), captureRequested(capture)
	{
	}

	const QString& configPath;
	const QDir& outputDir;
	const bool captureRequested;
	int failures = 0;
	int scenes = 0;
};

QPair<int, bool> cardVisualState(const QList<FilterCardRow*>& rows, const char* property, int expectedIndex)
{
	int count = 0;
	bool expected = false;
	for (int i = 0; i < rows.size(); i++)
	{
		CommandRowFrame* frame = rows[i]->findChild<CommandRowFrame*>(
			QString(), Qt::FindDirectChildrenOnly);
		const bool active = frame != nullptr && frame->property(property).toBool();
		if (active)
			count++;
		if (i == expectedIndex)
			expected = active;
	}
	return qMakePair(count, expected);
}

int checkHeaderClick(const QString& scene, FilterTable* table, const QList<FilterCardRow*>& rows)
{
	// Drive the exact FilterTable pointer path, then compare the model
	// answer with the dynamic properties the skin actually renders.
	const QPoint secondHeader = rows[1]->mapTo(table, rows[1]->getHeaderRect().center());
	cardClick(table, secondHeader);
	const bool headerModel = table->getSelectedItems().size() == 1
		&& table->getSelectedItems().contains(table->documentItems().at(1))
		&& table->getFocusedItem() == table->documentItems().at(1);
	const QPair<int, bool> headerSelected = cardVisualState(rows, "selected", 1);
	const QPair<int, bool> headerFocused = cardVisualState(rows, "focused", 1);
	const bool headerVisual = headerSelected.first == 1 && headerSelected.second
		&& headerFocused.first == 1 && headerFocused.second;
	if (!headerModel || !headerVisual)
	{
		qWarning("CardSelectionTest: %s header click model=%d visual=%d selectedFrames=%d focusedFrames=%d",
			qPrintable(scene), headerModel ? 1 : 0, headerVisual ? 1 : 0,
			headerSelected.first, headerFocused.first);
		return 1;
	}
	return 0;
}

int checkKeyboardMove(const QString& scene, FilterTable* table, const QList<FilterCardRow*>& rows)
{
	// Keyboard navigation is the closest regression path: it uses the
	// same state synchronizer but must retain the list's existing arrow
	// behavior after pointer clicks become card-aware.
	cardPressKey(table, Qt::Key_Up);
	const bool keyboardModel = table->getSelectedItems().size() == 1
		&& table->getSelectedItems().contains(table->documentItems().at(0))
		&& table->getFocusedItem() == table->documentItems().at(0);
	const QPair<int, bool> keyboardSelected = cardVisualState(rows, "selected", 0);
	const QPair<int, bool> keyboardFocused = cardVisualState(rows, "focused", 0);
	const bool keyboardVisual = keyboardSelected.first == 1 && keyboardSelected.second
		&& keyboardFocused.first == 1 && keyboardFocused.second;
	if (!keyboardModel || !keyboardVisual)
	{
		qWarning("CardSelectionTest: %s keyboard move model=%d visual=%d selectedFrames=%d focusedFrames=%d",
			qPrintable(scene), keyboardModel ? 1 : 0, keyboardVisual ? 1 : 0,
			keyboardSelected.first, keyboardFocused.first);
		return 1;
	}
	return 0;
}

int checkEditorClick(const QString& scene, FilterTable* table, const QList<FilterCardRow*>& rows)
{
	// QLineEdit consumes its own press. A plain click there must still
	// focus/select its owning card and collapse an old multi-selection.
	rows[2]->editText();
	QApplication::processEvents();
	table->selectAll();
	QApplication::processEvents();
	QLineEdit* rawEditor = rows[2]->findChild<QLineEdit*>(
		QStringLiteral("FilterCardRawEditor"));
	if (rawEditor == nullptr || !rawEditor->isVisible())
	{
		qWarning("CardSelectionTest: %s raw editor is unavailable", qPrintable(scene));
		return 1;
	}
	else
	{
		cardClick(rawEditor, rawEditor->rect().center());
		const bool editorModel = table->getSelectedItems().size() == 1
			&& table->getSelectedItems().contains(table->documentItems().at(2))
			&& table->getFocusedItem() == table->documentItems().at(2);
		const QPair<int, bool> editorSelected = cardVisualState(rows, "selected", 2);
		const QPair<int, bool> editorFocused = cardVisualState(rows, "focused", 2);
		const bool editorVisual = editorSelected.first == 1 && editorSelected.second
			&& editorFocused.first == 1 && editorFocused.second;
		if (!editorModel || !editorVisual)
		{
			qWarning("CardSelectionTest: %s editor click model=%d visual=%d selectedFrames=%d focusedFrames=%d",
				qPrintable(scene), editorModel ? 1 : 0, editorVisual ? 1 : 0,
				editorSelected.first, editorFocused.first);
			return 1;
		}
	}
	return 0;
}

// The tall-card table the focus checks share. It lives in the scene, so the
// popup timer of checkRawPickerStable writes into it, not into a dead frame.
struct FocusScene
{
	FocusScene(const QList<QString>& lines, QScrollArea& area, FilterTable* table, QScrollBar* scrollBar,
		FilterCardRow* subjectRow, QLineEdit* editor)
		: focusLines(lines), focusArea(area), focusTable(table), bar(scrollBar), subject(subjectRow),
		focusEditor(editor)
	{
	}

	const QList<QString>& focusLines;
	QScrollArea& focusArea;
	FilterTable* focusTable;
	QScrollBar* bar;
	FilterCardRow* subject;
	QLineEdit* focusEditor;
	QPointer<FilterCardRow> editedAnchor;
	bool anchorReplacedInPopup = false;
};

bool checkTallClick(const FocusScene& f)
{
	QScrollBar* bar = f.bar;
	FilterTable* focusTable = f.focusTable;
	FilterCardRow* subject = f.subject;
	bar->setValue(subject->y() + 100);
	const int clickScroll = bar->value();
	focusTable->selectOnlyFromCard(focusTable->documentItems().first());
	cardClick(f.focusEditor, QPoint(30, 120));
	const bool bodyClickStable = bar->value() == clickScroll;
	cardClick(focusTable, subject->mapTo(focusTable, QPoint(2, 190)));
	const bool tableClickStable = bar->value() == clickScroll;
	return subject->height() > f.focusArea.viewport()->height()
		&& clickScroll > 0 && bodyClickStable && tableClickStable;
}

bool checkFoldStable(const FocusScene& f)
{
	QScrollBar* bar = f.bar;
	FilterCardRow* subject = f.subject;
	QLineEdit* focusEditor = f.focusEditor;
	focusEditor->setMinimumHeight(0);
	subject->setMinimumHeight(0);
	subject->editText();
	QApplication::processEvents();
	focusEditor->setFocus(Qt::OtherFocusReason);
	bar->setValue(subject->y());
	const int foldScroll = bar->value();
	const bool hadBodyFocus = QApplication::focusWidget() == focusEditor;
	QMetaObject::invokeMethod(subject, "setExpanded", Qt::DirectConnection, Q_ARG(bool, false));
	QApplication::processEvents();
	return hadBodyFocus && foldScroll > 0
		&& bar->value() == foldScroll && QApplication::focusWidget() == f.focusTable;
}

bool checkReplaceStable(const FocusScene& f)
{
	QScrollBar* bar = f.bar;
	FilterTable* focusTable = f.focusTable;
	FilterCardRow* subject = f.subject;
	QLineEdit* focusEditor = f.focusEditor;
	subject->editText();
	QApplication::processEvents();
	focusEditor->setFocus(Qt::OtherFocusReason);
	bar->setValue(subject->y());
	const int replaceScroll = bar->value();
	const bool hadReplaceFocus = QApplication::focusWidget() == focusEditor;
	focusTable->updateSingleRowGui(focusTable->documentItems().at(2));
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QApplication::processEvents();
	return hadReplaceFocus && QApplication::focusWidget() == focusTable
		&& bar->value() == replaceScroll;
}

bool checkDismissStable(const FocusScene& f, int& pickerScroll)
{
	const QScrollBar* bar = f.bar;
	FilterTable* focusTable = f.focusTable;
	QScrollArea& focusArea = f.focusArea;
	// Dismissal must return to the requesting row, not a raw editor that
	// happened to have focus before the popup opened.
	QPointer<FilterCardRow> pickerAnchor = focusTable->findChildren<FilterCardRow*>(
		QString(), Qt::FindDirectChildrenOnly).first();
	QTimer::singleShot(0, focusTable, []() {
		if (QWidget* popup = QApplication::activePopupWidget())
			popup->hide();
	});
	FilterTemplate ignoredTemplate;
	pickerScroll = bar->value();
	const bool pickerChosen = focusTable->chooseFilterTemplate(&ignoredTemplate,
		focusArea.mapToGlobal(QPoint(20, 20)), pickerAnchor);
	QApplication::processEvents();
	// The offscreen plugin does not reactivate the owner after a popup.
	// Simulate activation, without choosing its focus widget for it.
	focusArea.activateWindow();
	QApplication::processEvents();
	const bool dismissStable = !pickerChosen && QApplication::focusWidget() == pickerAnchor
		&& bar->value() == pickerScroll;
	if (!dismissStable)
	{
		qWarning("CardSelectionTest: dismiss chosen=%d focus=%s anchor=%d scroll=%d/%d active=%d",
			pickerChosen, QApplication::focusWidget() != nullptr ? QApplication::focusWidget()->metaObject()->className() : "null",
			QApplication::focusWidget() == pickerAnchor, bar->value(), pickerScroll, focusArea.isActiveWindow());
	}
	return dismissStable;
}

bool checkRebuildStable(const FocusScene& f, int pickerScroll)
{
	FilterTable* focusTable = f.focusTable;
	focusTable->updateGuis();
	VSTPluginSession::waitForPendingLoads(30000);
	QApplication::processEvents();
	return QApplication::focusWidget() == focusTable
		&& f.bar->value() == pickerScroll;
}

bool checkRawPickerStable(FocusScene& f)
{
	FilterTable* focusTable = f.focusTable;
	FocusScene* const scene = &f;
	f.editedAnchor = focusTable->findChildren<FilterCardRow*>(
		QString(), Qt::FindDirectChildrenOnly).at(2);
	f.editedAnchor->editText();
	QApplication::processEvents();
	QLineEdit* pendingEditor = f.editedAnchor->findChild<QLineEdit*>(QStringLiteral("FilterCardRawEditor"));
	pendingEditor->selectAll();
	pendingEditor->insert(QStringLiteral("Preamp: -7 dB"));
	f.anchorReplacedInPopup = false;
	QTimer::singleShot(30, focusTable, [scene]() {
		scene->anchorReplacedInPopup = scene->editedAnchor.isNull();
		if (QWidget* popup = QApplication::activePopupWidget())
		{
			FilterPickerView* picker = popup->findChild<FilterPickerView*>();
			if (picker != nullptr)
				emit picker->entryChosen(0);
			else
				popup->hide();
		}
	});
	QMetaObject::invokeMethod(f.editedAnchor, "addAbove", Qt::DirectConnection);
	QApplication::processEvents();
	f.focusArea.activateWindow();
	QApplication::processEvents();
	return f.anchorReplacedInPopup
		&& focusTable->documentItems().size() == f.focusLines.size() + 1
		&& focusTable->documentItems().at(3)->text == QStringLiteral("Preamp: -7 dB")
		&& QApplication::focusWidget() == focusTable;
}

bool checkRemoveStable(const FocusScene& f, const QGridLayout* focusGrid)
{
	QScrollBar* bar = f.bar;
	FilterTable* focusTable = f.focusTable;
	FilterCardRow* removedRow = qobject_cast<FilterCardRow*>(focusGrid->itemAtPosition(3, 0)->widget());
	removedRow->editText();
	QApplication::processEvents();
	QLineEdit* removedEditor = removedRow->findChild<QLineEdit*>(QStringLiteral("FilterCardRawEditor"));
	removedEditor->selectAll();
	removedEditor->insert(QStringLiteral("Preamp: -9 dB"));
	bar->setValue(removedRow->y());
	const int removeScroll = bar->value();
	focusTable->removeLine(focusTable->documentItems().at(3));
	QApplication::processEvents();
	return QApplication::focusWidget() == focusTable
		&& bar->value() == removeScroll && focusTable->documentItems().size() == f.focusLines.size();
}

// #456: one click on the raw-line toggle (</>) opens the raw editor and
// the next one closes it. The press used to hand the focus to the list
// first, the raw editor closed itself on losing it, and the release
// then switched it straight back on, so the editor could not be left.
bool checkCodeToggleOnce(const QString& scene, const QGridLayout* focusGrid)
{
	bool codeToggleOnce = false;
	FilterCardRow* codeRow = qobject_cast<FilterCardRow*>(focusGrid->itemAtPosition(0, 0)->widget());
	QToolButton* toggle = nullptr;
	if (codeRow != nullptr)
	{
		for (QToolButton* button : codeRow->findChildren<QToolButton*>(QStringLiteral("FilterCardIconButton")))
			if (button->isCheckable() && !button->property("powerToggle").toBool())
				toggle = button;
	}
	QLineEdit* rawEditor = codeRow != nullptr
		? codeRow->findChild<QLineEdit*>(QStringLiteral("FilterCardRawEditor")) : nullptr;
	if (toggle != nullptr && rawEditor != nullptr)
	{
		const QPoint center = toggle->rect().center();
		const auto editing = [&]() { return toggle->isChecked() && rawEditor->isVisible(); };
		const auto idle = [&]() { return !toggle->isChecked() && !rawEditor->isVisible(); };
		cardClickWithFocus(toggle, center);
		const bool opened = editing();
		cardClickWithFocus(toggle, center);
		const bool closed = idle();
		cardClickWithFocus(toggle, center);
		const bool reopened = editing();
		cardClickWithFocus(toggle, center);
		const bool reclosed = idle();
		codeToggleOnce = opened && closed && reopened && reclosed;
		if (!codeToggleOnce)
			qWarning("CardSelectionTest: %s code toggle opened=%d closed=%d reopened=%d reclosed=%d",
				qPrintable(scene), opened, closed, reopened, reclosed);
	}
	return codeToggleOnce;
}

struct KnobGestureResult
{
	bool knobGrab = false;
	bool knobTurn = false;
	bool knobReset = false;
};

// #456: a press grabs a knob without moving it, wherever on the knob
// it lands, an arc knob then turns by the angle the pointer sweeps
// around its centre, and a double-click puts the gain back to 0 dB.
KnobGestureResult checkKnobGestures(const QString& scene, const FilterTable* focusTable, const QGridLayout* focusGrid)
{
	KnobGestureResult result;
	FilterCardRow* knobRow = qobject_cast<FilterCardRow*>(focusGrid->itemAtPosition(1, 0)->widget());
	AudioKnob* knob = knobRow != nullptr ? knobRow->findChild<AudioKnob*>(QStringLiteral("PreampCardKnob")) : nullptr;
	if (knob != nullptr)
	{
		bool& knobGrab = result.knobGrab;
		bool& knobTurn = result.knobTurn;
		bool& knobReset = result.knobReset;
		const QPointF centre = QRectF(knob->rect()).center();
		const QPointF rightEdge(knob->width() - 2.0, centre.y());
		const int before = knob->value();
		cardSendMouse(knob, QEvent::MouseButtonPress, rightEdge, Qt::LeftButton);
		knobGrab = knob->value() == before;
		cardSendMouse(knob, QEvent::MouseButtonRelease, rightEdge, Qt::NoButton);

		if (SkinManager::instance()->knobGesture() == KnobGesture::Rotary)
		{
			// A quarter turn clockwise well outside the minimum radius
			// is a third of the range.
			const double radius = KnobTravel::MinimumRadiusPixels * 2.0;
			const int grabbed = knob->value();
			cardSendMouse(knob, QEvent::MouseButtonPress, centre + QPointF(0.0, -radius), Qt::LeftButton);
			for (int step = 1; step <= 9; step++)
			{
				const double angle = step * 10.0 * 3.14159265358979323846 / 180.0;
				cardSendMouse(knob, QEvent::MouseMove,
					centre + QPointF(std::sin(angle) * radius, -std::cos(angle) * radius), Qt::LeftButton);
			}
			const int expected = std::min(knob->maximum(),
				grabbed + qRound((knob->maximum() - knob->minimum()) / 3.0));
			knobTurn = std::abs(knob->value() - expected) <= 1;
			cardSendMouse(knob, QEvent::MouseButtonRelease, centre + QPointF(radius, 0.0), Qt::NoButton);
			if (!knobTurn)
				qWarning("CardSelectionTest: %s knob turn %d -> %d (expected %d)",
					qPrintable(scene), grabbed, knob->value(), expected);

			// The same quarter turn on the painted knob itself, inside
			// the minimum radius, where the hand actually is: the value
			// follows the law's arc-length rate, counter-clockwise here.
			const double inside = std::min(knob->width(), knob->height()) / 2.0 - 6.0;
			const int insideGrabbed = knob->value();
			double law = insideGrabbed;
			QPointF previous(0.0, -inside);
			cardSendMouse(knob, QEvent::MouseButtonPress, centre + previous, Qt::LeftButton);
			for (int step = 1; step <= 9; step++)
			{
				const double angle = -step * 10.0 * 3.14159265358979323846 / 180.0;
				const QPointF next(std::sin(angle) * inside, -std::cos(angle) * inside);
				law = KnobTravel::turn(law, KnobTravel::sweptDegrees(previous.x(), previous.y(), next.x(), next.y()),
					knob->minimum(), knob->maximum(), false);
				previous = next;
				cardSendMouse(knob, QEvent::MouseMove, centre + next, Qt::LeftButton);
			}
			cardSendMouse(knob, QEvent::MouseButtonRelease, centre + previous, Qt::NoButton);
			const bool insideTurn = knob->value() == qRound(law) && knob->value() < insideGrabbed;
			if (!insideTurn)
				qWarning("CardSelectionTest: %s knob turn inside %d -> %d (law %.2f)",
					qPrintable(scene), insideGrabbed, knob->value(), law);
			knobTurn = knobTurn && insideTurn;
		}
		else
			knobTurn = true;

		cardSendMouse(knob, QEvent::MouseButtonPress, rightEdge, Qt::LeftButton);
		cardSendMouse(knob, QEvent::MouseButtonRelease, rightEdge, Qt::NoButton);
		cardSendMouse(knob, QEvent::MouseButtonDblClick, rightEdge, Qt::LeftButton);
		// A hand that moves a pixel straight outward before letting go
		// turns nothing in either gesture; it must not bring back the
		// value the first press grabbed.
		cardSendMouse(knob, QEvent::MouseMove, rightEdge + QPointF(1.0, 0.0), Qt::LeftButton);
		cardSendMouse(knob, QEvent::MouseButtonRelease, rightEdge + QPointF(1.0, 0.0), Qt::NoButton);
		QApplication::processEvents();
		const QString line = focusTable->documentItems().at(1)->text;
		knobReset = knob->value() == 0 && line.startsWith(QStringLiteral("Preamp: 0"));
		if (!knobReset)
			qWarning("CardSelectionTest: %s knob reset value=%d line=%s",
				qPrintable(scene), knob->value(), qPrintable(line));
	}
	return result;
}

// An open comment card shows its note once, in the body editor; the
// header carries it only while the card is closed.
bool checkCommentNoteOnce(const QString& configPath)
{
	QScrollArea noteArea;
	noteArea.resize(960, 240);
	QList<FilterCardRow*> noteRows = buildRows(noteArea, configPath, { QStringLiteral("# Living room preset") });
	const QLabel* noteSummary = noteRows.size() == 1
		? noteRows.first()->findChild<QLabel*>(QStringLiteral("FilterCardSummary")) : nullptr;
	bool noteOnce = false;
	if (noteSummary != nullptr)
	{
		QMetaObject::invokeMethod(noteRows.first(), "setExpanded", Qt::DirectConnection, Q_ARG(bool, true));
		const bool hiddenOpen = noteSummary->isHidden();
		QMetaObject::invokeMethod(noteRows.first(), "setExpanded", Qt::DirectConnection, Q_ARG(bool, false));
		// The shown scroll area makes isVisible() the closed-card fact
		// (and a different expression from the open-card read above).
		const bool shownClosed = noteSummary->isVisible();
		noteOnce = hiddenOpen && shownClosed && !noteSummary->text().isEmpty();
	}
	return noteOnce;
}

void runCardSelectionScene(CardSelectionRun& run, ISkin* skin, bool dark)
{
	const QString& configPath = run.configPath;
	int& failures = run.failures;
	const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
	const QString scene = QStringLiteral("%1/%2").arg(skin->id(), mode);
	SkinManager::instance()->applySkin(skin->id(), dark);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, {
		QStringLiteral("Preamp: -1 dB"),
		QStringLiteral("Preamp: -2 dB"),
		QStringLiteral("Preamp: -3 dB")
	});
	FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
	if (table == nullptr || rows.size() != 3)
	{
		qWarning("CardSelectionTest: %s table construction failed", qPrintable(scene));
		failures++;
		return;
	}

	failures += checkHeaderClick(scene, table, rows);
	failures += checkKeyboardMove(scene, table, rows);
	failures += checkEditorClick(scene, table, rows);

	if (run.captureRequested)
	{
		const QString shot = run.outputDir.filePath(QStringLiteral("%1_%2_card-selection.png")
			.arg(skin->id(), mode));
		if (!table->grab().save(shot, "PNG"))
		{
			qWarning("CardSelectionTest: %s could not write %s",
				qPrintable(scene), qPrintable(shot));
			failures++;
		}
	}

	// Keep enough rows below the subject that folding cannot clamp the
	// scrollbar merely because the document became shorter.
	QList<QString> focusLines;
	for (int i = 0; i < 16; i++)
		focusLines.append(QStringLiteral("Preamp: -1 dB"));
	QScrollArea focusArea;
	focusArea.resize(960, 320);
	QList<FilterCardRow*> focusRows = buildRows(focusArea, configPath, focusLines);
	FilterTable* focusTable = qobject_cast<FilterTable*>(focusArea.widget());
	focusTable->setFocusPolicy(Qt::StrongFocus);
	focusArea.activateWindow();
	QApplication::processEvents();
	QScrollBar* bar = focusArea.verticalScrollBar();
	FilterCardRow* subject = focusRows[2];
	subject->editText();
	QLineEdit* focusEditor = subject->findChild<QLineEdit*>(QStringLiteral("FilterCardRawEditor"));
	focusEditor->setMinimumHeight(900);
	subject->setMinimumHeight(900);
	QApplication::processEvents();
	focusTable->layout()->activate();
	QApplication::processEvents();
	FocusScene focus(focusLines, focusArea, focusTable, bar, subject, focusEditor);

	const bool tallClick = checkTallClick(focus);
	if (!tallClick)
		failures++;
	const bool foldStable = checkFoldStable(focus);
	if (!foldStable)
		failures++;
	const bool replaceStable = checkReplaceStable(focus);
	if (!replaceStable)
		failures++;
	int pickerScroll = 0;
	const bool dismissStable = checkDismissStable(focus, pickerScroll);
	if (!dismissStable)
		failures++;
	const bool rebuildStable = checkRebuildStable(focus, pickerScroll);
	if (!rebuildStable)
		failures++;
	const bool rawPickerStable = checkRawPickerStable(focus);
	if (!rawPickerStable)
		failures++;
	const QGridLayout* focusGrid = qobject_cast<QGridLayout*>(focusTable->layout());
	const bool removeStable = checkRemoveStable(focus, focusGrid);
	if (!removeStable)
		failures++;
	qWarning("CardSelectionTest: %s focus tall-click=%d fold=%d replace=%d dismiss=%d rebuild=%d raw-picker=%d remove=%d",
		qPrintable(scene), tallClick, foldStable, replaceStable, dismissStable, rebuildStable, rawPickerStable, removeStable);

	const bool codeToggleOnce = checkCodeToggleOnce(scene, focusGrid);
	if (!codeToggleOnce)
		failures++;
	const KnobGestureResult knob = checkKnobGestures(scene, focusTable, focusGrid);
	if (!knob.knobGrab || !knob.knobTurn || !knob.knobReset)
		failures++;
	qWarning("CardSelectionTest: %s code-toggle-once=%d knob grab=%d turn=%d reset=%d",
		qPrintable(scene), codeToggleOnce, knob.knobGrab, knob.knobTurn, knob.knobReset);

	const bool noteOnce = checkCommentNoteOnce(configPath);
	if (!noteOnce)
		failures++;
	qWarning("CardSelectionTest: %s comment note-once=%d", qPrintable(scene), noteOnce);

	qWarning("CardSelectionTest: %s complete", qPrintable(scene));
	run.scenes++;
}
}

namespace SkinGallery
{
int runCardSelectionTest(const QStringList& arguments)
{
	qWarning("CardSelectionTest: starting");
	QScreen* screen = QGuiApplication::primaryScreen();
	qWarning("CardSelectionTest: target platform=%s style=%s dpr=%.2f locale=%s",
		qPrintable(QGuiApplication::platformName()), qPrintable(qApp->style()->objectName()),
		screen != nullptr ? screen->devicePixelRatio() : 0.0, qPrintable(QLocale().name()));

	const int flagIndex = arguments.indexOf(QStringLiteral("--card-selection-test"));
	QDir outputDir;
	bool captureRequested = false;
	if (flagIndex >= 0 && flagIndex + 1 < arguments.size()
		&& !arguments.at(flagIndex + 1).startsWith(QStringLiteral("--")))
	{
		outputDir = QDir(arguments.at(flagIndex + 1));
		captureRequested = true;
		if (!outputDir.mkpath(QStringLiteral(".")))
		{
			qWarning("CardSelectionTest: cannot create output directory %s",
				qPrintable(outputDir.absolutePath()));
			return 2;
		}
	}

	const GalleryFixture fixture("CardSelectionTest");
	if (!fixture.isValid())
		return 2;

	CardSelectionRun run{ fixture.configPath(), outputDir, captureRequested };
	forEachSkinMode([&run](ISkin* skin, bool dark) { runCardSelectionScene(run, skin, dark); });

	qWarning("CardSelectionTest: %d scenes, failures %d", run.scenes, run.failures);
	return GalleryFixture::finish(run.failures == 0 ? 0 : 1);
}
}
