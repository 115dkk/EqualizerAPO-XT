/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The probes armed over the live MainWindow: --analysis-layout-test,
	--vst-panel-feed-test, --skin-metrics-probe and --window-shot.
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

bool SkinGallery::armAnalysisLayoutProbe(MainWindow& window, const QString& screenshotPath)
{
	QDockWidget* dock = window.findChild<QDockWidget*>(QStringLiteral("analysisDockWidget"));
	if (dock == nullptr)
	{
		fprintf(stderr, "Analysis layout test: required dock is missing\n");
		return false;
	}

	window.showNormal();
	window.resize(1024, 768);
	dock->show();
		QTimer::singleShot(750, &window, [&window, dock, screenshotPath]() {
		fprintf(stderr,
			"Analysis layout target: platform=%s style=%s dpr=%.2f locale=%s skin=%s dark=%d\n",
			qPrintable(QGuiApplication::platformName()), qPrintable(qApp->style()->objectName()),
			window.devicePixelRatioF(), qPrintable(QLocale().name()),
			qPrintable(SkinManager::instance()->currentSkinId()),
			SkinManager::instance()->isDark() ? 1 : 0);
		QWidget* controls = window.findChild<QWidget*>(QStringLiteral("analysisControlBar"));
		QWidget* graph = window.findChild<QWidget*>(QStringLiteral("ModernAnalysisGraph"));
		QBoxLayout* layout = window.findChild<QBoxLayout*>(QStringLiteral("analysisDockLayout"));
		const int centralWidth = window.centralWidget()->width();
		const int dockWidth = dock->width();
		const int controlsWidth = controls != nullptr ? controls->width() : -1;
		const int graphWidth = graph != nullptr ? graph->width() : -1;
		const bool centralProtected = centralWidth * 100 >= window.width() * 55;
		const bool dockBounded = dockWidth * 100 <= window.width() * 45;
		const bool graphUsable = graphWidth >= 300;
		const bool controlsFill = controlsWidth >= graphWidth - 2;
		const bool stacked = layout != nullptr && layout->direction() == QBoxLayout::TopToBottom;

		bool screenshotSaved = screenshotPath.isEmpty();
		if (!screenshotPath.isEmpty())
		{
			QFileInfo screenshotInfo(screenshotPath);
			QDir().mkpath(screenshotInfo.absolutePath());
			const QPixmap shot = window.grab();
			screenshotSaved = shot.save(screenshotPath);
		}

		const bool rightPass = centralProtected && dockBounded && graphUsable
			&& controlsFill && stacked && screenshotSaved;
		fprintf(stderr,
			"Analysis layout test (right): window=%dx%d central=%d dock=%d controls=%d graph=%d "
			"centralProtected=%d dockBounded=%d graphUsable=%d controlsFill=%d stacked=%d screenshot=%d\n",
			window.width(), window.height(), centralWidth, dockWidth, controlsWidth, graphWidth,
			centralProtected ? 1 : 0, dockBounded ? 1 : 0, graphUsable ? 1 : 0,
			controlsFill ? 1 : 0, stacked ? 1 : 0, screenshotSaved ? 1 : 0);

		const bool bottomRequested = QMetaObject::invokeMethod(&window,
			"on_graphPositionComboBox_currentIndexChanged", Qt::DirectConnection,
			Q_ARG(int, 1));
		QTimer::singleShot(250, &window, [&window, dock, rightPass, bottomRequested]() {
			QWidget* bottomControls = window.findChild<QWidget*>(QStringLiteral("analysisControlBar"));
			QBoxLayout* bottomLayout = window.findChild<QBoxLayout*>(QStringLiteral("analysisDockLayout"));
			const bool bottomArea = window.dockWidgetArea(dock) == Qt::BottomDockWidgetArea;
			const bool horizontal = bottomLayout != nullptr
				&& bottomLayout->direction() == QBoxLayout::LeftToRight;
			const bool compactControls = bottomControls != nullptr
				&& bottomControls->maximumWidth() < QWIDGETSIZE_MAX
				&& bottomControls->width() <= bottomControls->maximumWidth();
			fprintf(stderr,
				"Analysis layout test (bottom): area=%d horizontal=%d compactControls=%d\n",
				bottomArea ? 1 : 0, horizontal ? 1 : 0, compactControls ? 1 : 0);
			const bool bottomPass = bottomRequested && bottomArea
				&& horizontal && compactControls;
			const bool rightRestoreRequested = QMetaObject::invokeMethod(&window,
				"on_graphPositionComboBox_currentIndexChanged", Qt::DirectConnection,
				Q_ARG(int, 2));
			QTimer::singleShot(250, &window,
				[&window, dock, rightPass, bottomPass, rightRestoreRequested]() {
					QWidget* restoredControls = window.findChild<QWidget*>(
						QStringLiteral("analysisControlBar"));
					QWidget* restoredGraph = window.findChild<QWidget*>(
						QStringLiteral("ModernAnalysisGraph"));
					QBoxLayout* restoredLayout = window.findChild<QBoxLayout*>(
						QStringLiteral("analysisDockLayout"));
					const bool rightRestored = rightRestoreRequested
						&& window.dockWidgetArea(dock) == Qt::RightDockWidgetArea
						&& restoredLayout != nullptr
						&& restoredLayout->direction() == QBoxLayout::TopToBottom
						&& restoredControls != nullptr && restoredGraph != nullptr
						&& restoredControls->width() >= restoredGraph->width() - 2;
					fprintf(stderr, "Analysis layout test (right restored): pass=%d\n",
						rightRestored ? 1 : 0);
					const int exitCode = rightPass && bottomPass && rightRestored ? 0 : 1;
					bool holdOk = false;
					const int requestedHold = qEnvironmentVariableIntValue(
						"EAPO_ANALYSIS_LAYOUT_HOLD_MS", &holdOk);
					const int holdMs = holdOk ? qBound(0, requestedHold, 30000) : 0;
					if (holdMs > 0)
						QTimer::singleShot(holdMs, &window,
							[exitCode]() { QCoreApplication::exit(exitCode); });
					else
						QCoreApplication::exit(exitCode);
				});
		});
	});
	return true;
}

bool SkinGallery::armVstPanelFeedProbe(MainWindow& window, const QString& durationValue)
{
	bool durationOk = false;
	int durationMs = durationValue.toInt(&durationOk);
	if (!durationOk || durationMs <= 0)
		durationMs = 8000;

	window.showNormal();
	// Pin the window to the primary screen: a restored geometry on another
	// monitor would divorce the screen grab below from the widget rects.
	QScreen* primary = QGuiApplication::primaryScreen();
	window.setGeometry(QRect(primary->availableGeometry().topLeft() + QPoint(60, 60),
		QSize(1280, 900)));
	window.raise();
	window.activateWindow();

	// Let the configuration finish loading before the embed request.
	QTimer::singleShot(1500, &window, [&window, durationMs]() {
		// The session may have restored other configuration tabs; only the
		// active tab's widgets are visible, and the probe file was loaded
		// last, so the visible VST card is the probe's card.
		VSTCardEditor* card = nullptr;
		const QList<VSTCardEditor*> cards = window.findChildren<VSTCardEditor*>();
		for (VSTCardEditor* candidate : cards)
			if (candidate->isVisible())
				card = candidate;
		if (card == nullptr)
		{
			fprintf(stderr, "VstPanelFeedProbe: no visible VST card (%d total)\n",
				int(cards.size()));
			std::fflush(nullptr);
			std::_Exit(1);
		}
		if (!QMetaObject::invokeMethod(card, "embedToggled", Qt::DirectConnection, Q_ARG(bool, true)))
		{
			fprintf(stderr, "VstPanelFeedProbe: embed request failed\n");
			std::fflush(nullptr);
			std::_Exit(1);
		}

		// A settle pause for the plug-in view to attach and paint its first
		// frame, then the sampling run.
		QTimer::singleShot(2000, card, [card, durationMs]() {
			struct SampleState
			{
				QImage previous;
				QVector<double> diffs;
			};
			auto state = std::make_shared<SampleState>();
			const int intervalMs = 250;
			const int totalSamples = qMax(4, durationMs / intervalMs);
			QTimer* sampler = new QTimer(card);
			sampler->setInterval(intervalMs);
			QObject::connect(sampler, &QTimer::timeout, card, [card, state, totalSamples, sampler]() {
				// Composited screen pixels, because the plug-in renders into
				// native child windows QWidget::grab cannot see. grabWindow
				// offsets are relative to the screen, not the virtual desktop,
				// and the screen is the one actually containing the card.
				const QPoint globalTopLeft = card->mapToGlobal(QPoint(0, 0));
				QScreen* screen = QGuiApplication::screenAt(
					globalTopLeft + QPoint(card->width() / 2, card->height() / 2));
				if (screen == nullptr)
					screen = QGuiApplication::primaryScreen();
				const QPoint topLeft = globalTopLeft - screen->geometry().topLeft();
				const QPixmap shot = screen->grabWindow(0, topLeft.x(), topLeft.y(),
					card->width(), card->height());
				QImage image = shot.toImage().convertToFormat(QImage::Format_RGB32);
				if (state->previous.isNull())
					fprintf(stderr, "VstPanelFeedProbe: card=%dx%d at %d,%d image=%dx%d screen=%s\n",
						card->width(), card->height(), topLeft.x(), topLeft.y(),
						image.width(), image.height(), qPrintable(screen->name()));
				if (!state->previous.isNull() && state->previous.size() == image.size())
				{
					qint64 total = 0;
					qint64 sampled = 0;
					for (int y = 0; y < image.height(); y += 2)
					{
						const QRgb* current = reinterpret_cast<const QRgb*>(image.constScanLine(y));
						const QRgb* previous = reinterpret_cast<const QRgb*>(state->previous.constScanLine(y));
						for (int x = 0; x < image.width(); x += 2)
						{
							total += qAbs(qRed(current[x]) - qRed(previous[x]))
								+ qAbs(qGreen(current[x]) - qGreen(previous[x]))
								+ qAbs(qBlue(current[x]) - qBlue(previous[x]));
							sampled++;
						}
					}
					state->diffs.append(sampled > 0 ? double(total) / double(sampled) : 0.0);
				}
				state->previous = image;
				if (state->diffs.size() < totalSamples)
					return;

				sampler->stop();
				int activeFrames = 0;
				double maxDiff = 0.0;
				double sum = 0.0;
				QString series;
				for (double diff : state->diffs)
				{
					// A meter strip moving inside a mostly static card keeps
					// the per-sampled-pixel mean small; 0.05 sits well above
					// the measured no-feed noise floor (0.02) and well below
					// the measured feed signal (0.06-0.37).
					if (diff > 0.05)
						activeFrames++;
					maxDiff = qMax(maxDiff, diff);
					sum += diff;
					series += QString::number(diff, 'f', 2) + QLatin1Char(' ');
				}
				const double meanDiff = sum / state->diffs.size();
				// LIVE when a solid share of the sampled intervals moved: a
				// one-off repaint cannot fake a meter, while meter ballistics
				// legitimately hold still between peaks even on a modulated
				// signal.
				const bool live = activeFrames * 4 >= state->diffs.size();
				fprintf(stderr,
					"VstPanelFeedProbe: feedDisabled=%d frames=%d activeFrames=%d meanDiff=%.2f maxDiff=%.2f verdict=%s\n",
					qEnvironmentVariableIsSet("EAPO_DISABLE_PANEL_FEED") ? 1 : 0,
					int(state->diffs.size()), activeFrames, meanDiff, maxDiff, live ? "LIVE" : "STATIC");
				fprintf(stderr, "VstPanelFeedProbe: series=%s\n", qPrintable(series.trimmed()));
				std::fflush(nullptr);
				std::_Exit(0);
			});
			sampler->start();
		});
	});
	return true;
}

// ---------------------------------------------------------------------------
// --skin-metrics-probe (diagnostic): shrink the analysis dock, then walk the
// five skins on the live MainWindow and report the dock's size/minimums and
// which physical font face serves Korean text in the chrome. Not a gate.
// ---------------------------------------------------------------------------

#include <QFontDatabase>
#include <QFontInfo>
#include <QRawFont>
#include <QAction>

namespace
{
QAction* findSkinAction(const MainWindow& window, const QString& skinId)
{
	for (QAction* action : window.findChildren<QAction*>())
		if (action->isCheckable() && action->data().toString() == skinId)
			return action;
	return nullptr;
}

void reportFontDatabase()
{
	for (const QString& family : { QStringLiteral("Pretendard"), QStringLiteral("Pretendard Variable"),
		QStringLiteral("EAPO Sans KR"), QStringLiteral("EAPO Sans"), QStringLiteral("EAPO Mono"), QStringLiteral("EAPO Mono K"), QStringLiteral("Malgun Gothic"),
		QStringLiteral("Noto Sans KR"), QStringLiteral("Noto Sans") })
	{
		fprintf(stderr, "  family %s: has=%d private=%d korean=%d styles=",
			qPrintable(family), QFontDatabase::hasFamily(family) ? 1 : 0,
			QFontDatabase::isPrivateFamily(family) ? 1 : 0,
			QFontDatabase::writingSystems(family).contains(QFontDatabase::Korean) ? 1 : 0);
		for (const QString& style : QFontDatabase::styles(family))
			fprintf(stderr, "[%s w%d] ", qPrintable(style), QFontDatabase::weight(family, style));
		fprintf(stderr, "\n");
	}
}

void reportFontMatrix()
{
	const QList<QStringList> lists = {
		{ QStringLiteral("Pretendard") },
		{ QStringLiteral("EAPO Sans KR") },
		{ QStringLiteral("EAPO Sans"), QStringLiteral("EAPO Sans KR") },
		{ QStringLiteral("EAPO Mono"), QStringLiteral("EAPO Mono K"), QStringLiteral("EAPO Sans KR") },
		{ QStringLiteral("EAPO Sans"), QStringLiteral("EAPO Sans KR"), QStringLiteral("Noto Sans KR"),
			QStringLiteral("Noto Sans"), QStringLiteral("Malgun Gothic"), QStringLiteral("Microsoft YaHei"),
			QStringLiteral("sans-serif") }
	};
	for (const QStringList& list : lists)
	{
		for (int weight : { 400, 500, 600, 700 })
		{
			fprintf(stderr, "  matrix [%s] w=%d:", qPrintable(list.join(QLatin1Char(','))), weight);
			for (double pt : { 9.0, 10.0, 10.5, 11.0, 12.0, 14.0 })
			{
				QFont font;
				font.setFamilies(list);
				font.setWeight(QFont::Weight(weight));
				font.setPointSizeF(pt);
				const QRawFont korean = QRawFont::fromFont(font, QFontDatabase::Korean);
				fprintf(stderr, " %.1f=%s/%s", pt, qPrintable(korean.familyName()), qPrintable(korean.styleName()));
			}
			fprintf(stderr, "\n");
		}
	}
}

void reportFace(const char* label, const QWidget* widget)
{
	if (widget == nullptr)
	{
		fprintf(stderr, "    %s: (missing)\n", label);
		return;
	}
	const QFont font = widget->font();
	const QFontInfo info(font);
	const QRawFont latin = QRawFont::fromFont(font, QFontDatabase::Latin);
	const QRawFont korean = QRawFont::fromFont(font, QFontDatabase::Korean);
	fprintf(stderr, "    %s: request=[%s] w=%d pt=%.1f | info=%s w=%d | latin=%s/%s w=%d | korean=%s/%s w=%d\n",
		label, qPrintable(font.families().join(QLatin1Char(','))), int(font.weight()), font.pointSizeF(),
		qPrintable(info.family()), int(info.weight()),
		qPrintable(latin.familyName()), qPrintable(latin.styleName()), int(latin.weight()),
		qPrintable(korean.familyName()), qPrintable(korean.styleName()), int(korean.weight()));
}

void reportFonts(const MainWindow& window)
{
	reportFace("titlebar", window.findChild<QLabel*>(QStringLiteral("TitleBarText")));
	reportFace("menubar", window.findChild<QMenuBar*>());
	reportFace("cardtitle", window.findChild<QLabel*>(QStringLiteral("FilterCardTitle")));
	reportFace("formlabel", window.findChild<QLabel*>(QStringLiteral("AnalysisFormLabel")));
	QLabel probe(QStringLiteral("probe"));
	probe.ensurePolished();
	reportFace("plainlabel", &probe);
}

void reportDockMetrics(const MainWindow& window, const QDockWidget* dock)
{
	const QWidget* contents = dock->widget();
	const QWidget* controls = window.findChild<QWidget*>(QStringLiteral("analysisControlBar"));
	const QWidget* graph = window.findChild<QWidget*>(QStringLiteral("ModernAnalysisGraph"));
	const auto sz = [](const QSize& s) { return QStringLiteral("%1x%2").arg(s.width()).arg(s.height()); };
	fprintf(stderr, "    dock: size=%s minHint=%s min=%s hint=%s\n",
		qPrintable(sz(dock->size())), qPrintable(sz(dock->minimumSizeHint())),
		qPrintable(sz(dock->minimumSize())), qPrintable(sz(dock->sizeHint())));
	if (contents != nullptr)
		fprintf(stderr, "    contents: size=%s minHint=%s min=%s\n",
			qPrintable(sz(contents->size())), qPrintable(sz(contents->minimumSizeHint())),
			qPrintable(sz(contents->minimumSize())));
	if (controls != nullptr)
	{
		fprintf(stderr, "    controls: size=%s minHint=%s hint=%s\n",
			qPrintable(sz(controls->size())), qPrintable(sz(controls->minimumSizeHint())),
			qPrintable(sz(controls->sizeHint())));
		for (const QWidget* child : controls->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (!child->isVisibleTo(controls))
				continue;
			fprintf(stderr, "      %s(%s): size=%s minHint=%s hint=%s\n",
				qPrintable(child->objectName()), child->metaObject()->className(),
				qPrintable(sz(child->size())), qPrintable(sz(child->minimumSizeHint())),
				qPrintable(sz(child->sizeHint())));
		}
	}
	if (graph != nullptr)
		fprintf(stderr, "    graph: size=%s minHint=%s min=%s hint=%s\n",
			qPrintable(sz(graph->size())), qPrintable(sz(graph->minimumSizeHint())),
			qPrintable(sz(graph->minimumSize())), qPrintable(sz(graph->sizeHint())));
}

void settle()
{
	for (int i = 0; i < 4; i++)
	{
		QCoreApplication::sendPostedEvents();
		QApplication::processEvents(QEventLoop::AllEvents, 50);
	}
}
}

bool SkinGallery::armSkinMetricsProbe(MainWindow& window)
{
	QDockWidget* dock = window.findChild<QDockWidget*>(QStringLiteral("analysisDockWidget"));
	if (dock == nullptr)
	{
		fprintf(stderr, "Skin metrics probe: required dock is missing\n");
		return false;
	}
	window.showNormal();
	window.resize(1024, 768);
	dock->show();
	QTimer::singleShot(750, &window, [&window, dock]() {
		const QString originalSkin = SkinManager::instance()->currentSkinId();
		const Qt::DockWidgetArea area = window.dockWidgetArea(dock);
		fprintf(stderr, "Skin metrics probe: platform=%s skin=%s dark=%d area=%d appfont=[%s] w=%d\n",
			qPrintable(QGuiApplication::platformName()), qPrintable(originalSkin),
			SkinManager::instance()->isDark() ? 1 : 0, int(area),
			qPrintable(QApplication::font().families().join(QLatin1Char(','))), int(QApplication::font().weight()));
		reportFontDatabase();
		reportFontMatrix();
		const bool vertical = area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea;
		window.resizeDocks({ dock }, { vertical ? 170 : 330 }, vertical ? Qt::Vertical : Qt::Horizontal);
		settle();
		fprintf(stderr, "  after shrink: dock=%dx%d\n", dock->width(), dock->height());
		for (const QString& skinId : { QStringLiteral("studio"), QStringLiteral("minimal"),
			QStringLiteral("soft"), QStringLiteral("rack"), QStringLiteral("matrix"), QStringLiteral("studio") })
		{
			QAction* action = findSkinAction(window, skinId);
			if (action == nullptr)
			{
				fprintf(stderr, "  skin %s: no menu action\n", qPrintable(skinId));
				continue;
			}
			action->trigger();
			settle();
			fprintf(stderr, "  skin %s (active=%s):\n", qPrintable(skinId),
				qPrintable(SkinManager::instance()->currentSkinId()));
			reportDockMetrics(window, dock);
			reportFonts(window);
		}
		// Second pass with the dock on the right, where the minimum WIDTH is
		// what a shrunken dock runs into.
		const QComboBox* positionCombo = window.findChild<QComboBox*>(QStringLiteral("graphPositionComboBox"));
		const int originalPosition = positionCombo != nullptr ? positionCombo->currentIndex() : -1;
		QMetaObject::invokeMethod(&window, "on_graphPositionComboBox_currentIndexChanged",
			Qt::DirectConnection, Q_ARG(int, 2));
		settle();
		window.resizeDocks({ dock }, { 330 }, Qt::Horizontal);
		settle();
		fprintf(stderr, "  right dock after shrink: dock=%dx%d area=%d\n", dock->width(), dock->height(),
			int(window.dockWidgetArea(dock)));
		for (const QString& skinId : { QStringLiteral("studio"), QStringLiteral("minimal"),
			QStringLiteral("soft"), QStringLiteral("rack"), QStringLiteral("matrix"), QStringLiteral("studio") })
		{
			QAction* action = findSkinAction(window, skinId);
			if (action == nullptr)
				continue;
			action->trigger();
			settle();
			fprintf(stderr, "  right skin %s:\n", qPrintable(skinId));
			reportDockMetrics(window, dock);
		}
		if (originalPosition >= 0)
		{
			QMetaObject::invokeMethod(&window, "on_graphPositionComboBox_currentIndexChanged",
				Qt::DirectConnection, Q_ARG(int, originalPosition));
			settle();
		}
		if (QAction* original = findSkinAction(window, originalSkin))
		{
			original->trigger();
			settle();
		}
		QCoreApplication::exit(0);
	});
	return true;
}

// ---------------------------------------------------------------------------
// --window-shot <outDir> [--window-shot-skins a,b] [--window-shot-modes dark,light]
// [--window-shot-dock bottom|right] [--window-shot-width N] [--window-shot-height N]
// (diagnostic): grabs the live MainWindow, loaded from the positional
// config, once per skin and mode into <outDir>/<skin>_<mode>_window.png.
// The row gallery shows one row at a time; a review of composition (where
// the eye lands, where the light comes from) needs the whole window with a
// realistic config, which is what this renders. The skin and dark actions
// persist the user's choice, so the original pair is restored before exit.
// Judging material, not a gate.
// ---------------------------------------------------------------------------

namespace
{
QAction* findDarkThemeAction(const MainWindow& window)
{
	// The dark action has no object name; its shortcut is the stable handle.
	const QKeySequence darkShortcut(QStringLiteral("Ctrl+Alt+D"));
	for (QAction* action : window.findChildren<QAction*>())
		if (action->isCheckable() && action->shortcut() == darkShortcut)
			return action;
	return nullptr;
}

QString probeOptionValue(const QStringList& arguments, const QString& name, const QString& fallback)
{
	const int index = arguments.indexOf(name);
	return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : fallback;
}

void settleFor(int milliseconds)
{
	// Skin swaps rebuild every row and the analysis thread redraws the graph
	// asynchronously; a plain settle() grabs half-built windows.
	QElapsedTimer clock;
	clock.start();
	while (clock.elapsed() < milliseconds)
	{
		QCoreApplication::sendPostedEvents();
		QApplication::processEvents(QEventLoop::AllEvents, 50);
	}
}
}

bool SkinGallery::armWindowShotProbe(MainWindow& window, const QStringList& arguments)
{
	const QString outPath = probeOptionValue(arguments, QStringLiteral("--window-shot"), QString());
	if (outPath.isEmpty())
	{
		fprintf(stderr, "Window shot: --window-shot needs an output directory\n");
		return false;
	}
	QDockWidget* dock = window.findChild<QDockWidget*>(QStringLiteral("analysisDockWidget"));
	QAction* darkAction = findDarkThemeAction(window);
	if (dock == nullptr || darkAction == nullptr)
	{
		fprintf(stderr, "Window shot: analysis dock or dark theme action is missing\n");
		return false;
	}
	const QStringList skins = probeOptionValue(arguments, QStringLiteral("--window-shot-skins"),
		QStringLiteral("studio,minimal,soft,rack,matrix")).split(QLatin1Char(','), Qt::SkipEmptyParts);
	const QStringList modes = probeOptionValue(arguments, QStringLiteral("--window-shot-modes"),
		QStringLiteral("dark,light")).split(QLatin1Char(','), Qt::SkipEmptyParts);
	const bool dockRight = probeOptionValue(arguments, QStringLiteral("--window-shot-dock"),
		QStringLiteral("bottom")) == QStringLiteral("right");
	const int width = probeOptionValue(arguments, QStringLiteral("--window-shot-width"), QStringLiteral("1280")).toInt();
	const int height = probeOptionValue(arguments, QStringLiteral("--window-shot-height"), QStringLiteral("820")).toInt();
	// Dock extent (height for bottom, width for right); the default split
	// gives the dock most of a short window and hides the list.
	const int dockSize = probeOptionValue(arguments, QStringLiteral("--window-shot-dock-size"), QStringLiteral("300")).toInt();
	// 1-based card to select by clicking its header, 0 for none.
	const int selectCard = probeOptionValue(arguments, QStringLiteral("--window-shot-select"), QStringLiteral("0")).toInt();
	QDir outDir(outPath);
	if (!outDir.mkpath(QStringLiteral(".")))
	{
		fprintf(stderr, "Window shot: cannot create %s\n", qPrintable(outPath));
		return false;
	}

	window.showNormal();
	window.resize(qMax(640, width), qMax(480, height));
	dock->show();
	QTimer::singleShot(750, &window, [&window, dock, darkAction, skins, modes, dockRight, dockSize, selectCard, outDir]() {
		const QString originalSkin = SkinManager::instance()->currentSkinId();
		const bool originalDark = SkinManager::instance()->isDark();
		QMetaObject::invokeMethod(&window, "on_graphPositionComboBox_currentIndexChanged",
			Qt::DirectConnection, Q_ARG(int, dockRight ? 2 : 1));
		settleFor(600);
		window.resizeDocks({ dock }, { dockSize }, dockRight ? Qt::Horizontal : Qt::Vertical);
		// Without an audio device the analysis has no channel layout and draws
		// a flat line; a fixed stereo layout lets the response show.
		// The toolbar combos share one object name. The device combo is the
		// one without a stereo mask; its first real entry (the heading has
		// no data) gives the analysis a device, and then the layout combo's
		// stereo entry gives it two named channels to draw.
		const QList<QComboBox*> toolbarCombos = window.findChildren<QComboBox*>(QStringLiteral("ToolBarComboBox"));
		for (QComboBox* combo : toolbarCombos)
		{
			if (combo->findData(3) >= 0)
				continue;
			for (int i = 0; i < combo->count(); i++)
			{
				if (combo->itemData(i).isValid())
				{
					// The window listens to activated (a user choice), which
					// setCurrentIndex does not emit.
					combo->setCurrentIndex(i);
					Q_EMIT combo->activated(i);
					break;
				}
			}
		}
		settleFor(300);
		for (QComboBox* combo : toolbarCombos)
		{
			const int stereo = combo->findData(3);
			if (stereo >= 0)
			{
				combo->setCurrentIndex(stereo);
				Q_EMIT combo->activated(stereo);
				fprintf(stderr, "Window shot: layout '%s' (mask %d)\n",
					qPrintable(combo->currentText()), combo->currentData().toInt());
				break;
			}
		}
		// Analyse the loaded file itself, not the install's root config.
		// The .ui name is overridden for skinning (MainWindow renames the
		// analysis form combos), so the source combo is found by its shape:
		// the control bar's two-entry combo whose first entry is config.txt.
		QComboBox* startFrom = nullptr;
		if (QWidget* controls = window.findChild<QWidget*>(QStringLiteral("analysisControlBar")))
		{
			for (QComboBox* combo : controls->findChildren<QComboBox*>())
			{
				if (combo->count() == 2 && combo->itemText(0) == QStringLiteral("config.txt"))
				{
					startFrom = combo;
					break;
				}
			}
		}
		if (startFrom != nullptr)
		{
			startFrom->setCurrentIndex(1);
			Q_EMIT startFrom->activated(1);
			window.startAnalysis();
			settleFor(1500);
			fprintf(stderr, "Window shot: analysis from item %d of %d ('%s')\n", startFrom->currentIndex(),
				startFrom->count(), qPrintable(startFrom->currentText()));
		}
		settleFor(600);
		if (selectCard > 0)
		{
			const QList<QWidget*> headers = window.findChildren<QWidget*>(QStringLiteral("FilterCardHeader"));
			if (selectCard <= headers.size())
			{
				QWidget* header = headers.at(selectCard - 1);
				const QPoint localPos(header->width() / 2, header->height() / 2);
				const QPoint globalPos = header->mapToGlobal(localPos);
				QMouseEvent press(QEvent::MouseButtonPress, QPointF(localPos), QPointF(globalPos),
					Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
				QApplication::sendEvent(header, &press);
				QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(globalPos),
					Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
				QApplication::sendEvent(header, &release);
				settleFor(200);
			}
			else
				fprintf(stderr, "Window shot: only %d cards, cannot select %d\n", int(headers.size()), selectCard);
		}
		fprintf(stderr, "Window shot: platform=%s window=%dx%d dock=%s %dpx select=%d\n",
			qPrintable(QGuiApplication::platformName()), window.width(), window.height(),
			dockRight ? "right" : "bottom", dockSize, selectCard);
		int saved = 0;
		for (const QString& skinId : skins)
		{
			QAction* action = findSkinAction(window, skinId);
			if (action == nullptr)
			{
				fprintf(stderr, "  skin %s: no menu action\n", qPrintable(skinId));
				continue;
			}
			for (const QString& mode : modes)
			{
				const bool dark = mode == QStringLiteral("dark");
				if (darkAction->isChecked() != dark)
					darkAction->setChecked(dark);
				action->trigger();
				settleFor(1500);
				const QPixmap shot = window.grab();
				const QString file = outDir.filePath(QStringLiteral("%1_%2_window.png").arg(skinId, mode));
				const bool ok = shot.save(file);
				saved += ok ? 1 : 0;
				fprintf(stderr, "  %s %s: %dx%d %s\n", qPrintable(skinId), qPrintable(mode),
					shot.width(), shot.height(), ok ? "saved" : "NOT SAVED");
			}
		}
		if (darkAction->isChecked() != originalDark)
			darkAction->setChecked(originalDark);
		if (QAction* original = findSkinAction(window, originalSkin))
			original->trigger();
		settleFor(300);
		QCoreApplication::exit(saved == skins.size() * modes.size() ? 0 : 1);
	});
	return true;
}
