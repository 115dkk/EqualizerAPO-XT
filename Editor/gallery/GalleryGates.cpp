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
#include <numbers>
// For the two gates moved out of main.cpp (audit #275 B7): the VST
// round-trip self test and the analysis layout probe.
#include <optional>
#include <unordered_map>
#include <QBoxLayout>
#include <QDockWidget>
#include <QFileInfo>
#include <QLocale>
#include <QStyle>
#include <QTimer>
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "guis/VSTPluginFilterGUI.h"
#include "widgets/cards/VSTCardEditor.h"
#include "widgets/cards/VSTSlotFillRail.h"
#include "MainWindow.h"
#include "diagnostics/ToolbarPixelProbe.h"
#include "widgets/MainToolbarKit.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"
#include "widgets/subwooferrouting/SubwooferRoutingDefaults.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QRadioButton>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QTreeView>
#include <QUrl>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/guis/CopyFilterGUI.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/ISkin.h"
#include "Editor/skins/Skins.h"
#include "Editor/widgets/AddCardRow.h"
#include "Editor/widgets/EqGraphView.h"
#include "Editor/widgets/SegmentedControl.h"
#include "Editor/analysis/AnalysisMetric.h"
#include "Editor/analysis/AnalysisResponse.h"
#include "filters/BiQuad.h"
#include "Editor/skins/shared/SkinFileIcons.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/CommandRowFrame.h"
#include "Editor/widgets/FilterInsertSeam.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/SkinComboBox.h"
#include "Editor/widgets/TitleBar.h"
#include "Editor/widgets/UpdateToast.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingEditorDialog.h"
#include "Editor/widgets/cards/SubwooferRoutingCardEditor.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"

using namespace SkinGalleryDetail;

namespace SkinGallery
{
int runSwitchTest(const QStringList& arguments)
{
	Q_UNUSED(arguments);

	qWarning("SkinSwitchTest: starting");

	FilterInsertSeam accessibilitySeam;
	int seamActivations = 0;
	QObject::connect(&accessibilitySeam, &FilterInsertSeam::activated,
		[&seamActivations]() { seamActivations++; });
	QKeyEvent activateSeam(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
	QApplication::sendEvent(&accessibilitySeam, &activateSeam);
	if (accessibilitySeam.focusPolicy() != Qt::StrongFocus || seamActivations != 1
		|| accessibilitySeam.accessibleName().isEmpty())
	{
		qWarning("SkinSwitchTest: insertion seam lacks keyboard accessibility parity");
		return 1;
	}

	QWidget toastHost;
	UpdateToast timerProbe(&toastHost);
	timerProbe.showMessage(QStringLiteral("temporary"), 15000);
	const QTimer* const autoHideTimer = timerProbe.findChild<QTimer*>(QStringLiteral("UpdateToastAutoHide"));
	if (autoHideTimer == nullptr || !autoHideTimer->isActive())
	{
		qWarning("SkinSwitchTest: update toast does not own a restartable auto-hide timer");
		return 1;
	}
	timerProbe.showMessage(QStringLiteral("persistent"), 0);
	if (autoHideTimer->isActive())
	{
		qWarning("SkinSwitchTest: persistent update toast kept a stale auto-hide timer");
		return 1;
	}

	// Scratch reference targets so the reference cards resolve like the
	// gallery's; EAPO_SKIN_GALLERY also skips the audio-service ACL probe.
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("SkinSwitchTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("SkinSwitchTest: cannot write reference target files");
		return 2;
	}

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
	buildRows(scrollArea, configPath, lines);
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

	// Everything in the action train must stay alive and laid out: an action
	// reported invisible, an item widget QToolBarLayout hid (overflow into the
	// extension popup counts - the real toolbar never overflows at 1024px), or
	// a collapsed toolbar all reproduce the "toolbar is gone" field state.
	const auto checkToolbar = [&probeWindow, probeToolBar](const QString& switchName) {
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
	};

	// Generous ceiling: a healthy switch is well under a second offscreen,
	// the regression class this guards against cost multiple seconds per
	// switch, and CI runners are slow and variable. Overridable for local
	// tuning.
	bool limitOk = false;
	int limitMs = qEnvironmentVariableIntValue("EAPO_SWITCH_LIMIT_MS", &limitOk);
	if (!limitOk || limitMs <= 0)
		limitMs = 8000;
	bool warningOk = false;
	int warningMs = qEnvironmentVariableIntValue("EAPO_SWITCH_WARN_MS", &warningOk);
	if (!warningOk || warningMs <= 0 || warningMs >= limitMs)
		warningMs = 0;

	int failures = 0;
	const auto checkPaintOnlyChrome = [&scrollArea, &failures](const QString& objectName) {
		const QList<QWidget*> widgets = scrollArea.findChildren<QWidget*>(objectName);
		if (widgets.isEmpty())
		{
			qWarning("SkinSwitchTest: paint-only chrome %s is missing", qPrintable(objectName));
			failures++;
			return;
		}
		for (QWidget* widget : widgets)
		{
			if (!widget->testAttribute(Qt::WA_NoSystemBackground))
			{
				qWarning("SkinSwitchTest: paint-only chrome %s accepts a framework background",
					qPrintable(objectName));
				failures++;
				break;
			}
		}
	};
	QApplication::processEvents();
	failures += checkToolbar(QStringLiteral("baseline (before any switch)"));
	{
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
	}
	qint64 worstMs = 0;
	QString worstName;
	const int rounds = 3;
	for (int round = 1; round <= rounds; round++)
	{
		for (ISkin* skin : Skins::all())
		{
			for (int darkIndex = 0; darkIndex < 2; darkIndex++)
			{
				const bool dark = darkIndex == 0;
				const QString name = QStringLiteral("%1/%2").arg(skin->id(), dark ? QStringLiteral("dark") : QStringLiteral("light"));

				QElapsedTimer timer;
				timer.start();
				// MainWindow::skinSelected's exact live sequence: tear the
				// rows down BEFORE the global stylesheet swap (which also
				// re-derives the palette), rebuild after.
				table->clearRows();
				const qint64 clearMs = timer.restart();
				SkinManager::instance()->applySkin(skin->id(), dark);
				const qint64 applyMs = timer.restart();
				table->updateGuis();
				QApplication::processEvents();
				// The live editor returns to the event loop between switches,
				// which is when deleteLater victims (combo popup containers,
				// editor internals) actually die; a bare processEvents() does
				// not deliver DeferredDelete, and without this the harness
				// accumulates a dead generation per switch that the real app
				// never keeps.
				QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
				QApplication::processEvents();
				const qint64 rebuildMs = timer.elapsed();
				const qint64 elapsed = clearMs + applyMs + rebuildMs;

				if (SkinManager::instance()->currentSkinId() != skin->id())
				{
					qWarning("SkinSwitchTest: switch to %s resolved to %s",
						qPrintable(name), qPrintable(SkinManager::instance()->currentSkinId()));
					failures++;
				}
				if (skin->id() == QLatin1String("soft"))
					checkPaintOnlyChrome(QStringLiteral("SoftReferenceTile"));
				{
					// Caption ink check. tintedIcon paints every covered pixel
					// in the ink colour, so the strongest-coverage pixel must
					// sit on the active skin's text colour. Tolerance, not
					// equality: the thin minimize stroke has no fully opaque
					// pixel at 14 px and unpremultiply rounding shifts channels
					// by a few counts - a stale light/dark ink is off by ~100+.
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
						failures++;
					}
				}
				failures += checkToolbar(name);
				if (round == 1 && darkIndex == 0)
				{
					// The skinned file dialog's icon provider must serve the
					// drive glyph for both drive spellings: the real root
					// ("C:/") and the slash-less shell name ("C:") that
					// QFileSystemModel stores drive nodes under and rebuilds
					// node icons from on setIconProvider. The bare spelling
					// is neither isRoot() nor a file, which dressed every
					// sidebar drive with the folder pictogram in the field.
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
								qPrintable(skin->id()));
							failures++;
						}
					}
				}
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
				if (elapsed > worstMs)
				{
					worstMs = elapsed;
					worstName = name;
				}
				qWarning("SkinSwitchTest: round %d %s: %lld ms (clear %lld, apply %lld, rebuild %lld, widgets %lld)",
					round, qPrintable(name), static_cast<long long>(elapsed),
					static_cast<long long>(clearMs), static_cast<long long>(applyMs), static_cast<long long>(rebuildMs),
					static_cast<long long>(QApplication::allWidgets().size()));
			}
		}
	}

	{
		// Diagnostic: class histogram of the surviving widgets, top entries.
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
	}

	qWarning("SkinSwitchTest: %d switches over %d rows, worst %lld ms (%s), warning %d ms, limit %d ms, failures %d",
		rounds * int(Skins::all().size()) * 2, int(lines.size()), static_cast<long long>(worstMs),
		qPrintable(worstName), warningMs, limitMs, failures);

	// Same no-teardown exit as run(): everything is flushed, and unwinding
	// the offscreen QApplication can hang the process on a leftover resource.
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

int runCardMoveTest(const QStringList& arguments)
{
	Q_UNUSED(arguments);

	qWarning("CardMoveTest: starting");

	// Scratch reference targets so the reference cards resolve like the
	// gallery's; EAPO_SKIN_GALLERY also skips the audio-service ACL probe.
	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("CardMoveTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("CardMoveTest: cannot write reference target files");
		return 2;
	}

	// The same 100+ row document as the switch test: the field lag needs a
	// loaded document, and six copies of the representative rows exercise
	// every card type.
	QList<QString> lines;
	for (int repeat = 0; repeat < 6; repeat++)
		for (const GalleryRow& row : galleryRows())
			lines.append(row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	buildRows(scrollArea, configPath, lines);
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
	for (ISkin* skin : Skins::all())
	{
		for (int darkIndex = 0; darkIndex < 2; darkIndex++)
		{
			const bool dark = darkIndex == 0;
			const QString name = QStringLiteral("%1/%2").arg(skin->id(),
				dark ? QStringLiteral("dark") : QStringLiteral("light"));

			// Fresh rows under this skin, in the live switch order (tear down
			// before the stylesheet swap, rebuild after).
			table->clearRows();
			SkinManager::instance()->applySkin(skin->id(), dark);
			table->updateGuis();
			QApplication::processEvents();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();

			const int rowCount = int(table->documentItems().count());
			if (rowCount != lines.size())
			{
				qWarning("CardMoveTest: %s expected %lld rows, found %d",
					qPrintable(name), static_cast<long long>(lines.size()), rowCount);
				failures++;
				continue;
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
				table->moveRows({ moved }, dropRow);
				QApplication::processEvents();
				const qint64 elapsed = timer.elapsed();
				moves++;

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
		}
	}

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
			check("full rebuild", before, settle());
			// The live skin switch tears down first and rebuilds after the
			// stylesheet swap (MainWindow::skinSelected); the position must
			// survive that gap too.
			before = bar->value();
			table->clearRows();
			SkinManager::instance()->applySkin(Skins::all().first()->id(), false);
			table->updateGuis();
			check("skin switch", before, settle());
		}
	}

	// Same no-teardown exit as run().
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}

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

	QTemporaryDir scratch;
	if (!scratch.isValid())
	{
		qWarning("CardSelectionTest: cannot create a scratch directory");
		return 2;
	}
	qputenv("EAPO_SKIN_GALLERY", "1");
	const QString configPath = buildReferenceFiles(QDir(scratch.path()));
	if (configPath.isEmpty())
	{
		qWarning("CardSelectionTest: cannot write reference target files");
		return 2;
	}

	const auto click = [](QWidget* target, const QPoint& localPos) {
		const QPoint globalPos = target->mapToGlobal(localPos);
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(localPos), QPointF(globalPos),
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(target, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(globalPos),
			Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(target, &release);
		QApplication::processEvents();
	};
	const auto pressKey = [](QWidget* target, int key) {
		QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
		QApplication::sendEvent(target, &press);
		QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
		QApplication::sendEvent(target, &release);
		QApplication::processEvents();
	};

	int failures = 0;
	int scenes = 0;
	for (ISkin* skin : Skins::all())
	{
		for (int darkIndex = 0; darkIndex < 2; darkIndex++)
		{
			const bool dark = darkIndex == 0;
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
				continue;
			}

			const auto visualState = [&rows](const char* property, int expectedIndex) {
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
			};

			// Drive the exact FilterTable pointer path, then compare the model
			// answer with the dynamic properties the skin actually renders.
			const QPoint secondHeader = rows[1]->mapTo(table, rows[1]->getHeaderRect().center());
			click(table, secondHeader);
			const bool headerModel = table->getSelectedItems().size() == 1
				&& table->getSelectedItems().contains(table->documentItems().at(1))
				&& table->getFocusedItem() == table->documentItems().at(1);
			const QPair<int, bool> headerSelected = visualState("selected", 1);
			const QPair<int, bool> headerFocused = visualState("focused", 1);
			const bool headerVisual = headerSelected.first == 1 && headerSelected.second
				&& headerFocused.first == 1 && headerFocused.second;
			if (!headerModel || !headerVisual)
			{
				qWarning("CardSelectionTest: %s header click model=%d visual=%d selectedFrames=%d focusedFrames=%d",
					qPrintable(scene), headerModel ? 1 : 0, headerVisual ? 1 : 0,
					headerSelected.first, headerFocused.first);
				failures++;
			}

			// Keyboard navigation is the closest regression path: it uses the
			// same state synchronizer but must retain the list's existing arrow
			// behavior after pointer clicks become card-aware.
			pressKey(table, Qt::Key_Up);
			const bool keyboardModel = table->getSelectedItems().size() == 1
				&& table->getSelectedItems().contains(table->documentItems().at(0))
				&& table->getFocusedItem() == table->documentItems().at(0);
			const QPair<int, bool> keyboardSelected = visualState("selected", 0);
			const QPair<int, bool> keyboardFocused = visualState("focused", 0);
			const bool keyboardVisual = keyboardSelected.first == 1 && keyboardSelected.second
				&& keyboardFocused.first == 1 && keyboardFocused.second;
			if (!keyboardModel || !keyboardVisual)
			{
				qWarning("CardSelectionTest: %s keyboard move model=%d visual=%d selectedFrames=%d focusedFrames=%d",
					qPrintable(scene), keyboardModel ? 1 : 0, keyboardVisual ? 1 : 0,
					keyboardSelected.first, keyboardFocused.first);
				failures++;
			}

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
				failures++;
			}
			else
			{
				click(rawEditor, rawEditor->rect().center());
				const bool editorModel = table->getSelectedItems().size() == 1
					&& table->getSelectedItems().contains(table->documentItems().at(2))
					&& table->getFocusedItem() == table->documentItems().at(2);
				const QPair<int, bool> editorSelected = visualState("selected", 2);
				const QPair<int, bool> editorFocused = visualState("focused", 2);
				const bool editorVisual = editorSelected.first == 1 && editorSelected.second
					&& editorFocused.first == 1 && editorFocused.second;
				if (!editorModel || !editorVisual)
				{
					qWarning("CardSelectionTest: %s editor click model=%d visual=%d selectedFrames=%d focusedFrames=%d",
						qPrintable(scene), editorModel ? 1 : 0, editorVisual ? 1 : 0,
						editorSelected.first, editorFocused.first);
					failures++;
				}
			}

			if (captureRequested)
			{
				const QString shot = outputDir.filePath(QStringLiteral("%1_%2_card-selection.png")
					.arg(skin->id(), mode));
				if (!table->grab().save(shot, "PNG"))
				{
					qWarning("CardSelectionTest: %s could not write %s",
						qPrintable(scene), qPrintable(shot));
					failures++;
				}
			}

			qWarning("CardSelectionTest: %s complete", qPrintable(scene));
			scenes++;
		}
	}

	qWarning("CardSelectionTest: %d scenes, failures %d", scenes, failures);
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}
}
