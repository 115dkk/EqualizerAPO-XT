/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The --skin-gallery renderer: every gallery row in its states and the
	registered chrome scenes, per skin and mode, plus the heritage dumps
	(SkinGallery::run).
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

namespace
{
// renderSkin() emits every galleryRows() entry in kStatesPerRow states and
// every fixed scene registered by galleryScenarios(). run() derives the
// expected output count from those two sources and checks it.
constexpr int kStatesPerRow = 3;

struct GalleryScenario
{
	QString id;
	QStringList states;
};

// Fixed chrome scenarios are registered once. The render blocks below build
// their specialized widgets, while this table owns scenario identity, state
// vocabulary, and the deterministic shot-count contract.
const QList<GalleryScenario>& galleryScenarios()
{
	static const QList<GalleryScenario> scenarios = {
		{ QStringLiteral("picker"), { QStringLiteral("normal"), QStringLiteral("hover"),
			QStringLiteral("empty"), QStringLiteral("phasetime") } },
		{ QStringLiteral("srdialog"), { QStringLiteral("default"),
			QStringLiteral("preset"), QStringLiteral("expanded") } },
		{ QStringLiteral("toolbar"), { QStringLiteral("normal") } },
		{ QStringLiteral("analysis"), { QStringLiteral("normal") } },
		{ QStringLiteral("titlebar"), { QStringLiteral("normal") } },
		{ QStringLiteral("menubar"), { QStringLiteral("normal") } },
		{ QStringLiteral("menu"), { QStringLiteral("normal") } },
		{ QStringLiteral("addrow"), { QStringLiteral("normal"), QStringLiteral("hover") } },
		{ QStringLiteral("seam"), { QStringLiteral("hover") } },
		{ QStringLiteral("toast"), { QStringLiteral("normal") } },
		{ QStringLiteral("loadnotice"), { QStringLiteral("normal") } },
		{ QStringLiteral("filedialog"), { QStringLiteral("normal") } },
		{ QStringLiteral("graph"), { QStringLiteral("normal"), QStringLiteral("cursor"),
			QStringLiteral("phase"), QStringLiteral("groupdelay") } },
		{ QStringLiteral("segment"), { QStringLiteral("normal"), QStringLiteral("selected"),
			QStringLiteral("hover") } },
		{ QStringLiteral("copyfold"), { QStringLiteral("normal"), QStringLiteral("empty"),
			QStringLiteral("expanded"), QStringLiteral("editor"), QStringLiteral("sourceeditor") } },
		{ QStringLiteral("multiconvfold"), { QStringLiteral("normal"),
			QStringLiteral("expanded") } },
		{ QStringLiteral("logic"), { QStringLiteral("normal") } },
		{ QStringLiteral("setuperror"), { QStringLiteral("normal") } },
		{ QStringLiteral("channelscope"), { QStringLiteral("normal") } },
		{ QStringLiteral("velvet-advanced"), { QStringLiteral("normal") } },
		{ QStringLiteral("velvet-narrow"), { QStringLiteral("normal") } },
		{ QStringLiteral("controls"), { QStringLiteral("normal") } }
	};
	return scenarios;
}

int fixedScenarioShotCount()
{
	int count = 0;
	for (const GalleryScenario& scenario : galleryScenarios())
		count += scenario.states.size();
	return count;
}

std::shared_ptr<const AnalysisResponse> galleryAnalysisResponse();

QWidget* buildAnalysisPanelReplica(QWidget* parent)
{
	QWidget* panel = new QWidget(parent);
	QHBoxLayout* dockLayout = new QHBoxLayout(panel);
	dockLayout->setContentsMargins(10, 6, 10, 10);
	dockLayout->setSpacing(8);

	QFrame* bar = new QFrame;
	bar->setObjectName(QStringLiteral("analysisControlBar"));
	bar->setAttribute(Qt::WA_StyledBackground, true);
	bar->setMaximumWidth(MainWindow::analysisControlBarWidth);
	QGridLayout* grid = new QGridLayout(bar);
	// Matches MainWindow.ui after the metric switch and the base-delay option
	// joined this bar: the two extra rows are paid for by tightening the
	// rhythm and by pairing the four readouts two to a row, so the row count
	// stays at nine; the cap grew with the type scale (250 -> 280px).
	grid->setContentsMargins(10, 6, 18, 6);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(4);
	grid->setColumnStretch(1, 1);

	const QStringList formLabels = { QStringLiteral("From"), QStringLiteral("Channel"),
		QStringLiteral("Res"), QStringLiteral("Pos") };
	const QStringList formValues = { QStringLiteral("config.txt"), QStringLiteral("L"),
		QString(), QStringLiteral("Bottom") };
	for (int row = 0; row < formLabels.size(); row++)
	{
		QLabel* label = new QLabel(formLabels[row]);
		label->setObjectName(QStringLiteral("AnalysisFormLabel"));
		grid->addWidget(label, row, 0);
		if (formValues[row].isEmpty())
		{
			// The resolution field: MainWindow's ExponentialSpinBox paints as a
			// plain QSpinBox (only the stepping differs), so the replica keeps
			// the lighter widget under the same object name.
			QSpinBox* spin = new QSpinBox;
			spin->setObjectName(QStringLiteral("AnalysisFormSpin"));
			spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
			spin->setRange(128, 8388608);
			spin->setValue(65536);
			grid->addWidget(spin, row, 1);
		}
		else
		{
			QComboBox* combo = new QComboBox;
			combo->setObjectName(QStringLiteral("AnalysisFormCombo"));
			combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
			combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
			combo->setMinimumContentsLength(6);
			combo->addItem(formValues[row]);
			grid->addWidget(combo, row, 1);
		}
	}

	SegmentedControl* metricSegment = new SegmentedControl;
	metricSegment->setLabels({ QStringLiteral("Mag"), QStringLiteral("Phase"), QStringLiteral("GD") });
	grid->addWidget(metricSegment, 4, 0, 1, 2);

	QCheckBox* includeBaseDelay = new QCheckBox(QStringLiteral("Include base delay"));
	includeBaseDelay->setObjectName(QStringLiteral("AnalysisFormCheck"));
	grid->addWidget(includeBaseDelay, 5, 0, 1, 2);

	const QStringList statLabels = { QStringLiteral("Peak"), QStringLiteral("Lat"),
		QStringLiteral("Init"), QStringLiteral("CPU") };
	const QStringList statValues = { QStringLiteral("-6.0 dB"), QStringLiteral("0.0 ms"),
		QStringLiteral("0.4 ms"), QStringLiteral("0.1 %") };
	for (int i = 0; i < statLabels.size(); i++)
	{
		QFrame* chipFrame = new QFrame;
		chipFrame->setObjectName(QStringLiteral("AnalysisStatChip"));
		chipFrame->setAttribute(Qt::WA_StyledBackground, true);
		QHBoxLayout* chipLayout = new QHBoxLayout(chipFrame);
		chipLayout->setContentsMargins(8, 3, 8, 3);
		chipLayout->setSpacing(6);
		QLabel* label = new QLabel(statLabels[i]);
		label->setObjectName(QStringLiteral("AnalysisStatLabel"));
		chipLayout->addWidget(label);
		QLabel* value = new QLabel(statValues[i]);
		value->setObjectName(QStringLiteral("AnalysisStatValue"));
		value->setProperty("severity", QStringLiteral("normal"));
		chipLayout->addWidget(value);

		const int row = 6 + i / 2;
		QHBoxLayout* pairLayout = qobject_cast<QHBoxLayout*>(
			grid->itemAtPosition(row, 0) == nullptr ? nullptr : grid->itemAtPosition(row, 0)->layout());
		if (pairLayout == nullptr)
		{
			pairLayout = new QHBoxLayout;
			pairLayout->setSpacing(6);
			grid->addLayout(pairLayout, row, 0, 1, 2);
		}
		pairLayout->addWidget(chipFrame);
	}
	grid->setRowStretch(8, 1);

	EqGraphView* graph = new EqGraphView(panel);
	graph->setObjectName(QStringLiteral("ModernAnalysisGraph"));
	// Fed the same synthetic spectrum as the standalone graph shots. Left
	// empty, this replica used to draw a flat line across the middle, because
	// a response with no data read as a perfectly flat 0 dB one - a measurement
	// the analyzer had never taken. An unanalyzed graph now draws no trace at
	// all, which is correct and which would make this shot an empty pane.
	graph->setResponse(galleryAnalysisResponse(), QStringLiteral("All"));
	dockLayout->addWidget(bar);
	dockLayout->addWidget(graph, 1);
	return panel;
}

// The gallery's analysis fixture, as a complex spectrum.
//
// The graph consumes what the analyzer produces - a complex response per FFT bin
// - so the fixture has to be one too, not a hand-drawn dB curve. These are the
// same nine breakpoints the fixture has always used (boosts, cuts and a
// clipping shelf so the over-0dB emphasis shows); the curve between them is
// straight in dB against log frequency, and each bin simply samples it. Phase
// is left at zero: this fixture exists for the magnitude shots, and a metric
// that needs phase gets its own fixture.
std::shared_ptr<const AnalysisResponse> galleryAnalysisResponse()
{
	struct Breakpoint { double hz; double db; };
	static const Breakpoint curve[] = {
		{20.0, 0.0}, {45.0, 5.5}, {120.0, 2.0}, {300.0, -4.5}, {900.0, 1.0},
		{2500.0, -7.5}, {6000.0, 3.0}, {11000.0, 6.5}, {20000.0, -2.0}
	};
	constexpr int breakpointCount = int(sizeof(curve) / sizeof(curve[0]));

	const auto dbAt = [&](double hz) {
		if (hz <= curve[0].hz)
			return curve[0].db;
		if (hz >= curve[breakpointCount - 1].hz)
			return curve[breakpointCount - 1].db;
		for (int i = 1; i < breakpointCount; i++)
		{
			if (hz > curve[i].hz)
				continue;
			const double t = std::log(hz / curve[i - 1].hz) / std::log(curve[i].hz / curve[i - 1].hz);
			return curve[i - 1].db + t * (curve[i].db - curve[i - 1].db);
		}
		return curve[breakpointCount - 1].db;
	};

	auto response = std::make_shared<AnalysisResponse>();
	response->sampleRate = 48000;
	response->fftSize = 65536;
	const size_t binCount = AnalysisResponse::binCountFor(response->fftSize);
	response->bins.resize(binCount);
	for (size_t i = 0; i < binCount; i++)
	{
		const double hz = response->frequencyOf(i);
		response->bins[i] = std::complex<double>(std::pow(10.0, dbAt(hz) / 20.0), 0.0);
	}
	return response;
}

// A real 2nd-order all-pass, evaluated on the unit circle from the engine's own
// coefficients. Flat in magnitude by construction, so it is the fixture that
// makes the phase and group-delay shots show something a magnitude plot cannot:
// a full turn of phase around 1 kHz and a delay peak sitting on it.
std::shared_ptr<const AnalysisResponse> galleryAllPassResponse()
{
	auto response = std::make_shared<AnalysisResponse>();
	response->sampleRate = 48000;
	response->fftSize = 65536;
	response->bins.resize(AnalysisResponse::binCountFor(response->fftSize));

	BiQuad biquad(BiQuad::ALL_PASS, 0.0, 1000.0, response->sampleRate, 0.707, false);
	double packed[4];
	double b0 = 0.0;
	biquad.getCoefficients(packed, b0);
	for (size_t i = 0; i < response->bins.size(); i++)
	{
		const double omega = 2.0 * std::numbers::pi_v<double> * response->frequencyOf(i) / response->sampleRate;
		const std::complex<double> z1 = std::polar(1.0, -omega);
		const std::complex<double> z2 = z1 * z1;
		response->bins[i] = (b0 + packed[0] * z1 + packed[1] * z2)
			/ (1.0 + packed[2] * z1 + packed[3] * z2);
	}
	return response;
}

// QSS :hover matches widgets whose Qt::WA_UnderMouse attribute is set, and
// custom paint code reads the same attribute via underMouse(). Setting it
// manually lets the offscreen renderer capture the hover look without a real
// cursor. Pseudo-states are evaluated at paint time, so update() suffices.
void setHoverEquivalent(QWidget* root, bool on)
{
	root->setAttribute(Qt::WA_UnderMouse, on);
	for (QWidget* child : root->findChildren<QWidget*>())
		child->setAttribute(Qt::WA_UnderMouse, on);
	root->update();
}

// Overflow gate: a row must fit its 960px viewport in every skin. A visible
// horizontal scrollbar inside the row is the overflow defect this guards;
// failing the render makes CI keep the broken shot as evidence instead of
// shipping it silently.
int assertNoHorizontalScrollBar(QWidget* row, const QString& skinId, const QString& mode,
	const QString& rowName, const QString& state)
{
	for (const QScrollBar* bar : row->findChildren<QScrollBar*>())
	{
		if (bar->orientation() == Qt::Horizontal && bar->isVisible())
		{
			// For a scroll bar, pageStep is the viewport width and maximum the
			// hidden remainder, so content = maximum + pageStep.
			qWarning("SkinGallery: horizontal scrollbar in row %s_%s_%s_%s (content %d overflows viewport %d)",
				qPrintable(skinId), qPrintable(mode), qPrintable(rowName), qPrintable(state),
				bar->maximum() + bar->pageStep(), bar->pageStep());
			return 1;
		}
	}
	return 0;
}

bool saveGrab(QWidget* row, const QDir& outDir, const QString& skinId, const QString& mode,
	const QString& rowName, const QString& state)
{
	const QString fileName = QStringLiteral("%1_%2_%3_%4.png").arg(skinId, mode, rowName, state);
	QPixmap pixmap = row->grab();
	if (pixmap.isNull())
	{
		qWarning("SkinGallery: grab failed for %s", qPrintable(fileName));
		return false;
	}
	if (!pixmap.save(outDir.filePath(fileName), "PNG"))
	{
		qWarning("SkinGallery: could not write %s", qPrintable(fileName));
		return false;
	}
	return true;
}

int renderStates(const QDir& outDir, const QString& skinId, const QString& mode,
	const QString& configPath, const QList<GalleryRow>& rows, bool commented)
{
	QList<QString> lines;
	for (const GalleryRow& row : rows)
		lines.append(commented ? QStringLiteral("# ") + row.line : row.line);

	QScrollArea scrollArea;
	scrollArea.resize(960, 720);
	QList<FilterCardRow*> rowWidgets = buildRows(scrollArea, configPath, lines);
	if (rowWidgets.size() != rows.size())
	{
		qWarning("SkinGallery: expected %lld rows, got %lld (%s %s)",
			static_cast<long long>(rows.size()), static_cast<long long>(rowWidgets.size()),
			qPrintable(skinId), qPrintable(mode));
		return 1;
	}

	int failures = 0;
	for (int i = 0; i < rowWidgets.size(); i++)
	{
		FilterCardRow* row = rowWidgets[i];
		if (commented)
		{
			// A commented-out line is the product's real disabled state: power
			// toggle off, body editor disabled, muted chrome.
			failures += assertNoHorizontalScrollBar(row, skinId, mode, rows[i].name, QStringLiteral("disabled"));
			failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("disabled")) ? 0 : 1;
			continue;
		}

		failures += assertNoHorizontalScrollBar(row, skinId, mode, rows[i].name, QStringLiteral("normal"));
		failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("normal")) ? 0 : 1;
		setHoverEquivalent(row, true);
		failures += assertNoHorizontalScrollBar(row, skinId, mode, rows[i].name, QStringLiteral("hover"));
		failures += saveGrab(row, outDir, skinId, mode, rows[i].name, QStringLiteral("hover")) ? 0 : 1;
		setHoverEquivalent(row, false);
	}
	return failures;
}

int renderSkin(const QDir& outDir, const QString& skinId, const QString& configPath, bool dark)
{
	SkinManager::instance()->applySkin(skinId, dark);
	if (SkinManager::instance()->currentSkinId() != skinId)
	{
		// Skins::byId silently falls back to studio for unknown ids; a typo in
		// --skin-gallery-skins must fail loudly instead of producing duplicate
		// studio shots under a wrong name.
		qWarning("SkinGallery: unknown skin id '%s'", qPrintable(skinId));
		return 1;
	}

	const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
	int failures = 0;
	failures += renderStates(outDir, skinId, mode, configPath, galleryRows(), false);
	failures += renderStates(outDir, skinId, mode, configPath, galleryRows(), true);

	// The skin's "add filter" picker with the real template set, captured the
	// same way the rows are. A throwaway FilterTable supplies the entries from
	// the same command catalog chooseFilterTemplate consults at runtime.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, configPath, { QStringLiteral("Preamp: -6 dB") });
		const FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		FilterPickerView* picker = SkinManager::instance()->createFilterPicker(nullptr);
		picker->setEntries(table != nullptr ? table->filterPickerEntries() : QList<FilterPickerEntry>());
		picker->adjustSize();
		picker->show();
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("normal")) ? 0 : 1;
		// Showcase states. Pickers that have not implemented a state render
		// their normal look (base no-op), so the shot count stays fixed.
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::HoverFirstEntry);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("hover")) ? 0 : 1;
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::EmptySearch);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("empty")) ? 0 : 1;
		// The Phase & Time group. Without this the gallery only shows that the
		// all-pass left the parametric list, not where it went - and the list
		// is taller than the picker, so the group is below the fold in the
		// resting shot.
		picker->galleryShowcase(FilterPickerView::GalleryShowcase::PhaseAndTimeSearch);
		QApplication::processEvents();
		failures += saveGrab(picker, outDir, skinId, mode, QStringLiteral("picker"), QStringLiteral("phasetime")) ? 0 : 1;
		delete picker;
	}

	// The full subwoofer-routing editor dialog, in the two states a user meets
	// first: the seeded default for a stereo+LFE endpoint, and the built-in
	// #246 preset. The dialog hosts the skin's routing renderer twice plus
	// the response view, so it is judged per skin like the picker.
	{
		const subroute::PresetCreateResult preset =
			subroute::createBuiltInPreset(
				subroute::kIssue246FrontRear41PresetId);
		const struct
		{
			QString state;
			subroute::SubwooferRoutingState value;
			bool expandRouting = false;
		} dialogStates[] = {
			{ QStringLiteral("default"),
				subwooferroutingeditor::buildDefaultState(
					{ L"L", L"R", L"LFE" }), false },
			{ QStringLiteral("preset"),
				preset.succeeded()
					? *preset.state
					: subwooferroutingeditor::buildDefaultState(
						{ L"L", L"R", L"LFE" }), false },
			{ QStringLiteral("expanded"),
				preset.succeeded()
					? *preset.state
					: subwooferroutingeditor::buildDefaultState(
						{ L"L", L"R", L"LFE" }), true }
		};
		if (!preset.succeeded())
		{
			qWarning("SkinGallery: built-in subwoofer-routing preset failed "
				"to instantiate; the srdialog preset shot falls back to "
				"the default state");
			failures++;
		}
		for (const auto& dialogState : dialogStates)
		{
			SubwooferRoutingEditorDialog dialog(dialogState.value, 48000);
			dialog.resize(1360, 810);
			dialog.show();
			QApplication::processEvents();

			const QWidget* presetFocus = dialog.findChild<QWidget*>(
				QStringLiteral("SubwooferRoutingPresetCombo"));
			if (presetFocus == nullptr || dialog.focusWidget() != presetFocus)
			{
				qWarning("SkinGallery: subwoofer-routing dialog did not start "
					"on the preset editor (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures++;
			}

			const QList<RoutingView*> routingViews =
				dialog.findChildren<RoutingView*>();
			if (dialogState.expandRouting)
			{
				for (RoutingView* view : routingViews)
					view->galleryShowcase(QStringLiteral("expanded"));
				QApplication::processEvents();

				QScrollArea* contentScroll = dialog.findChild<QScrollArea*>(
					QStringLiteral("SubwooferRoutingContentScroll"));
				QWidget* buttons = dialog.findChild<QWidget*>(
					QStringLiteral("SubwooferRoutingButtonBox"));
				const bool needsVerticalScroll =
					skinId != QStringLiteral("studio");
				if (contentScroll == nullptr
					|| (needsVerticalScroll
						&& contentScroll->verticalScrollBar()->maximum() <= 0)
					|| buttons == nullptr || !buttons->isVisibleTo(&dialog)
					|| buttons->geometry().bottom()
						> dialog.contentsRect().bottom())
				{
					qWarning("SkinGallery: expanded subwoofer-routing dialog "
						"did not contain overflow or keep its actions visible "
						"(%s %s)", qPrintable(skinId), qPrintable(mode));
					failures++;
				}
			}
			failures += saveGrab(&dialog, outDir, skinId, mode,
				QStringLiteral("srdialog"), dialogState.state) ? 0 : 1;

			// Copy's add-channel field commits with Enter. Exercise the real
			// inline editor after the screenshot and prove the same key does not
			// accept the containing dialog.
			if (!routingViews.isEmpty())
			{
				routingViews.front()->galleryShowcase(
					QStringLiteral("addChannel"));
				QApplication::processEvents();
				QPointer<QLineEdit> inlineEditor =
					qobject_cast<QLineEdit*>(QApplication::focusWidget());
				if (inlineEditor == nullptr)
				{
					qWarning("SkinGallery: Copy add-channel editor did not "
						"take focus (%s %s)",
						qPrintable(skinId), qPrintable(mode));
					failures++;
				}
				else
				{
					QKeyEvent enterPress(QEvent::KeyPress,
						Qt::Key_Return, Qt::NoModifier);
					QApplication::sendEvent(inlineEditor, &enterPress);
					QApplication::processEvents();
				}
				if (!dialog.isVisible()
					|| dialog.result() == QDialog::Accepted)
				{
					qWarning("SkinGallery: Copy add-channel Enter accepted "
						"the subwoofer-routing dialog (%s %s)",
						qPrintable(skinId), qPrintable(mode));
					failures++;
				}
			}
			dialog.close();
			QApplication::processEvents();
		}
	}

	// The skin's main-toolbar chrome on a faithful replica (same object names
	// and widget train as MainWindow, dummy device data).
	{
		QToolBar* toolBar = buildToolbarReplica(nullptr);
		toolBar->resize(960, toolBar->sizeHint().height());
		toolBar->show();
		QApplication::processEvents();
		failures += saveGrab(toolBar, outDir, skinId, mode, QStringLiteral("toolbar"), QStringLiteral("normal")) ? 0 : 1;
		delete toolBar;
	}

	// The analysis dock's settings cell beside the graph, judged per skin
	// like the toolbar.
	{
		QWidget* panel = buildAnalysisPanelReplica(nullptr);
		panel->resize(960, 300);
		panel->show();
		QApplication::processEvents();
		failures += saveGrab(panel, outDir, skinId, mode, QStringLiteral("analysis"), QStringLiteral("normal")) ? 0 : 1;
		delete panel;
	}

	// Window chrome: the custom title bar over a dummy host. The Korean text
	// in the title is deliberate - it makes Hangul clipping/shaping defects
	// (reported from the field as "설정" rendering like "ㅅ정") visible in the
	// gallery on every machine, including CI.
	{
		QWidget host;
		host.setWindowTitle(QStringLiteral("Equalizer APO Configuration Editor — 설정.txt"));
		TitleBar* bar = new TitleBar(&host, nullptr);
		bar->resize(960, bar->sizeHint().height());
		bar->show();
		QApplication::processEvents();
		failures += saveGrab(bar, outDir, skinId, mode, QStringLiteral("titlebar"), QStringLiteral("normal")) ? 0 : 1;
		delete bar;
	}

	// Menu bar replica with the real top-level titles plus a Korean sample.
	{
		QMenuBar* menuBar = new QMenuBar(nullptr);
		menuBar->setObjectName(QStringLiteral("GalleryMenuBar"));
		menuBar->addMenu(QStringLiteral("File"));
		menuBar->addMenu(QStringLiteral("Edit"));
		menuBar->addMenu(QStringLiteral("View"));
		menuBar->addMenu(QStringLiteral("Settings"));
		menuBar->addMenu(QStringLiteral("설정"));
		menuBar->resize(960, menuBar->sizeHint().height());
		menuBar->show();
		QApplication::processEvents();
		failures += saveGrab(menuBar, outDir, skinId, mode, QStringLiteral("menubar"), QStringLiteral("normal")) ? 0 : 1;
		delete menuBar;
	}

	// An open dropdown menu with representative content: modern tinted icons,
	// a checkable item, a separator, a disabled item and a Korean label.
	{
		const QColor ink(SkinManager::instance()->tokens().text);
		QMenu* menu = new QMenu();
		menu->setObjectName(QStringLiteral("GalleryMenu"));
		menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/cut.svg"), ink, 18), QStringLiteral("Cut"));
		menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/copy.svg"), ink, 18), QStringLiteral("Copy"));
		menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/paste.svg"), ink, 18), QStringLiteral("Paste"));
		menu->addSeparator();
		QAction* checkable = menu->addAction(QStringLiteral("설정 항목 (Instant mode)"));
		checkable->setCheckable(true);
		checkable->setChecked(true);
		QAction* disabled = menu->addAction(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/trash.svg"), ink, 18), QStringLiteral("Delete"));
		disabled->setEnabled(false);
		menu->show();
		QApplication::processEvents();
		failures += saveGrab(menu, outDir, skinId, mode, QStringLiteral("menu"), QStringLiteral("normal")) ? 0 : 1;
		delete menu;
	}

	// List-level insertion chrome (shared insertion contract,
	// docs/skins/README.md), judged per skin like the toolbar: the trailing
	// add row at rest and under the cursor, and the first-boundary seam in
	// its hover reveal (at rest it deliberately paints nothing, so a rest
	// shot would only ever be a blank strip).
	{
		AddCardRow addRow;
		addRow.resize(960, addRow.sizeHint().height());
		addRow.show();
		QApplication::processEvents();
		// The offscreen platform parks the cursor at (0,0), which lands inside
		// this window and delivers a synthetic Enter on show, and window
		// activation hands the row keyboard focus - both dress the "normal"
		// shot as hover+focus. Clear both so the at-rest state is what gets
		// judged.
		QEvent addRowLeave(QEvent::Leave);
		QApplication::sendEvent(&addRow, &addRowLeave);
		addRow.clearFocus();
		QApplication::processEvents();
		failures += saveGrab(&addRow, outDir, skinId, mode, QStringLiteral("addrow"), QStringLiteral("normal")) ? 0 : 1;
		QEnterEvent addRowEnter(QPointF(480, 10), QPointF(480, 10), QPointF(480, 10));
		QApplication::sendEvent(&addRow, &addRowEnter);
		QApplication::processEvents();
		failures += saveGrab(&addRow, outDir, skinId, mode, QStringLiteral("addrow"), QStringLiteral("hover")) ? 0 : 1;
	}
	{
		FilterInsertSeam seam;
		seam.resize(960, 10);
		seam.show();
		QEnterEvent seamEnter(QPointF(20, 5), QPointF(20, 5), QPointF(20, 5));
		QApplication::sendEvent(&seam, &seamEnter);
		QApplication::processEvents();
		failures += saveGrab(&seam, outDir, skinId, mode, QStringLiteral("seam"), QStringLiteral("hover")) ? 0 : 1;
	}

	// The auto-update toast over a plain palette host, with the real message
	// template so per-skin QSS is judged against representative text.
	{
		QWidget host;
		host.resize(960, 90);
		host.setAutoFillBackground(true);
		UpdateToast* toast = new UpdateToast(&host);
		host.show();
		toast->showMessage(QStringLiteral("Update 2.99.0 has been downloaded and will be applied when you close the editor."), 0);
		QApplication::processEvents();
		failures += saveGrab(toast, outDir, skinId, mode, QStringLiteral("toast"), QStringLiteral("normal")) ? 0 : 1;
	}

	// The same toast carrying the load notice: a line whose filter could not be
	// set up kept the whole configuration from being applied. No skin styles it
	// apart from the update notice yet; this shot is the baseline for that round.
	{
		QWidget host;
		host.resize(960, 90);
		host.setAutoFillBackground(true);
		UpdateToast* toast = new UpdateToast(&host);
		host.show();
		toast->showMessage(QStringLiteral("This configuration was not applied: the filter on line 12 of room.txt could not be prepared. Equalizer APO keeps playing the previous settings."), 0);
		QApplication::processEvents();
		failures += saveGrab(toast, outDir, skinId, mode, QStringLiteral("loadnotice"), QStringLiteral("normal")) ? 0 : 1;
	}

	// Every stateful form control the skins restyle, in every state that has
	// its own QSS rule: check/partial/disabled checkboxes and radio buttons.
	// This is the shot that catches a stylesheet whose checked box is only a
	// filled square (the engine replaces native indicator drawing, so the
	// check glyph must come from the sheet itself).
	{
		QWidget controls;
		controls.setAutoFillBackground(true);
		QHBoxLayout* controlsLayout = new QHBoxLayout(&controls);
		controlsLayout->setContentsMargins(16, 12, 16, 12);
		controlsLayout->setSpacing(18);

		QCheckBox* checkedBox = new QCheckBox(QStringLiteral("Checked"), &controls);
		checkedBox->setChecked(true);
		controlsLayout->addWidget(checkedBox);

		QCheckBox* uncheckedBox = new QCheckBox(QStringLiteral("Unchecked"), &controls);
		controlsLayout->addWidget(uncheckedBox);

		QCheckBox* partialBox = new QCheckBox(QStringLiteral("Partial"), &controls);
		partialBox->setTristate(true);
		partialBox->setCheckState(Qt::PartiallyChecked);
		controlsLayout->addWidget(partialBox);

		QCheckBox* disabledBox = new QCheckBox(QStringLiteral("Disabled"), &controls);
		disabledBox->setChecked(true);
		disabledBox->setEnabled(false);
		controlsLayout->addWidget(disabledBox);

		QRadioButton* radioOn = new QRadioButton(QStringLiteral("Radio on"), &controls);
		radioOn->setChecked(true);
		controlsLayout->addWidget(radioOn);

		QRadioButton* radioOff = new QRadioButton(QStringLiteral("Radio off"), &controls);
		// Two radios in one widget share a button group; keep the second one
		// out of it so the first stays checked.
		radioOff->setAutoExclusive(false);
		controlsLayout->addWidget(radioOff);

		controlsLayout->addStretch(1);
		controls.resize(760, controls.sizeHint().height());
		controls.show();
		QApplication::processEvents();
		failures += saveGrab(&controls, outDir, skinId, mode, QStringLiteral("controls"), QStringLiteral("normal")) ? 0 : 1;
	}

	// The skinned file dialog (GUIHelper::prepareFileDialog): Qt's widget
	// dialog under the app-wide sheet, its navigation buttons dressed through
	// ISkin::styleFileDialog. The fixture's pinned timestamps keep the Detail
	// columns deterministic, and the sidebar is overridden with fixture-local
	// folders so its labels do not depend on this machine's user folders.
	{
		const QString fixturePath = buildFileDialogFixture(galleryFixtureRoot());
		if (fixturePath.isEmpty())
		{
			qWarning("SkinGallery: could not build the file dialog fixture");
			failures += 1;
		}
		else
		{
			QFileDialog dialog(nullptr, QStringLiteral("Open file"), fixturePath, QStringLiteral("*.txt"));
			dialog.setFileMode(QFileDialog::ExistingFiles);
			dialog.setNameFilter(QStringLiteral("E-APO configurations (*.txt)"));
			GUIHelper::prepareFileDialog(dialog);
			dialog.setSidebarUrls({ QUrl::fromLocalFile(QDir(fixturePath).filePath(QStringLiteral("config"))),
				QUrl::fromLocalFile(QDir(fixturePath).filePath(QStringLiteral("IRs"))) });
			dialog.show();
			// QFileSystemModel lists directories on a worker thread; wait for
			// the four fixture rows (config, IRs, demo, voice) to land before
			// grabbing.
			QTreeView* view = dialog.findChild<QTreeView*>(QStringLiteral("treeView"));
			QElapsedTimer listTimer;
			listTimer.start();
			while (listTimer.elapsed() < 3000
				&& (view == nullptr || view->model() == nullptr || view->model()->rowCount(view->rootIndex()) < 4))
				QApplication::processEvents(QEventLoop::AllEvents, 50);
			failures += saveGrab(&dialog, outDir, skinId, mode, QStringLiteral("filedialog"), QStringLiteral("normal")) ? 0 : 1;
		}
	}

	// The analysis dock's response graph over the deterministic synthetic
	// spectrum (boosts, cuts and a clipping shelf so the over-0dB emphasis
	// shows), at rest and with the pinned cursor readout.
	{
		EqGraphView graph;
		graph.resize(940, 220);
		graph.setResponse(galleryAnalysisResponse(), QStringLiteral("All"));
		graph.show();
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("normal")) ? 0 : 1;
		graph.setPreviewCursor(0.62);
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("cursor")) ? 0 : 1;
	}

	// The same graph showing what a magnitude plot cannot: an all-pass's phase
	// and its group delay. The fixture is a real 2nd-order all-pass evaluated
	// from the engine's own coefficients, so these shots show the filter rather
	// than a drawing of one, and the cursor is pinned to prove the readout
	// changes unit with the metric.
	{
		EqGraphView graph;
		graph.resize(940, 220);
		graph.setResponse(galleryAllPassResponse(), QStringLiteral("All"));
		graph.setPreviewCursor(0.62);
		graph.show();
		graph.setMetric(AnalysisMetric::PhaseDegrees);
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("phase")) ? 0 : 1;
		graph.setMetric(AnalysisMetric::GroupDelayMs);
		QApplication::processEvents();
		failures += saveGrab(&graph, outDir, skinId, mode, QStringLiteral("graph"), QStringLiteral("groupdelay")) ? 0 : 1;
	}

	// The metric switch itself, in its three positions plus hover, so the new
	// segmented control is judged as a control and not only in situ.
	{
		SegmentedControl segment;
		segment.setLabels({ QStringLiteral("Mag"), QStringLiteral("Phase"), QStringLiteral("GD") });
		segment.resize(230, segment.sizeHint().height());
		segment.show();
		// setPreviewState rather than setCurrentIndex: the indicator animates,
		// and a shot taken while it travels differs from run to run.
		segment.setPreviewState(0, -1);
		QApplication::processEvents();
		failures += saveGrab(&segment, outDir, skinId, mode, QStringLiteral("segment"), QStringLiteral("normal")) ? 0 : 1;
		segment.setPreviewState(1, -1);
		QApplication::processEvents();
		failures += saveGrab(&segment, outDir, skinId, mode, QStringLiteral("segment"), QStringLiteral("selected")) ? 0 : 1;
		segment.setPreviewState(1, 2);
		QApplication::processEvents();
		failures += saveGrab(&segment, outDir, skinId, mode, QStringLiteral("segment"), QStringLiteral("hover")) ? 0 : 1;
	}

	// The Copy channel fold over a synthetic 7.1 endpoint. The row matrix's
	// card path is deliberately deviceless, so the device-channel seeding
	// (and therefore the fold: collapsed rows, the reveal control, the
	// add-channel entry) never shows there. Four states: the routed line
	// collapsed to its two involved channels, the same line fully expanded,
	// the add-channel editor open with a name typed, and an empty Copy
	// showing its two representative channels.
	{
		auto surround = std::make_shared<GalleryAPOInfo>(
			L"Speakers", L"Example Audio 7.1", false, true, 8, 0x63F);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("Copy: VC=0.5*L+0.5*R R=L"), QStringLiteral("Copy:") },
			surround, 0x63F);
		if (rows.size() != 2)
		{
			qWarning("SkinGallery: copy fold scene expected 2 rows, got %lld (%s %s)",
				static_cast<long long>(rows.size()), qPrintable(skinId), qPrintable(mode));
			failures += 4;
		}
		else
		{
			FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
			auto settle = [&]() {
				QApplication::processEvents();
				if (table != nullptr && table->layout() != nullptr)
					table->layout()->activate();
				QApplication::processEvents();
			};
			failures += assertNoHorizontalScrollBar(rows[0], skinId, mode, QStringLiteral("copyfold"), QStringLiteral("normal"));
			failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("normal")) ? 0 : 1;
			failures += saveGrab(rows[1], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("empty")) ? 0 : 1;
			RoutingView* view = rows[0]->findChild<RoutingView*>();
			if (view == nullptr)
			{
				qWarning("SkinGallery: copy fold scene has no routing view (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures += 2;
			}
			else
			{
				view->galleryShowcase(QStringLiteral("expanded"));
				settle();
				failures += assertNoHorizontalScrollBar(rows[0], skinId, mode, QStringLiteral("copyfold"), QStringLiteral("expanded"));
				failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("expanded")) ? 0 : 1;
				view->galleryShowcase(QStringLiteral("addChannel"));
				settle();
				failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("editor")) ? 0 : 1;
				// The inline source editor on the routed line's first summand,
				// next to the open prompt. Only the minimal step list has one;
				// the other renderers edit in place and record the same view
				// again here, which keeps the shot-count contract uniform.
				view->galleryShowcase(QStringLiteral("editSource:VC"));
				settle();
				failures += saveGrab(rows[0], outDir, skinId, mode, QStringLiteral("copyfold"), QStringLiteral("sourceeditor")) ? 0 : 1;
			}
		}
	}

	// MultiConvolution shares Copy's target-channel fold, but its source side
	// is the selected WAV's fixed channel list. This 4-channel BRIR over a 7.1
	// endpoint proves both halves of that contract in every renderer: the
	// collapsed card lists only mapped L/R outputs while retaining source
	// ports 0..3, and the reveal control expands all eight outputs.
	{
		auto surround = std::make_shared<GalleryAPOInfo>(
			L"Speakers", L"Example Audio 7.1", false, true, 8, 0x63F);
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		QList<FilterCardRow*> rows = buildRows(scrollArea, configPath,
			{ QStringLiteral("MultiConvolution: L=0+1 R=2+3 brir.wav") },
			surround, 0x63F);
		if (rows.size() != 1)
		{
			qWarning("SkinGallery: MultiConvolution fold scene expected 1 row, got %lld (%s %s)",
				static_cast<long long>(rows.size()), qPrintable(skinId), qPrintable(mode));
			failures += 2;
		}
		else
		{
			FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
			auto settle = [&]() {
				QApplication::processEvents();
				if (table != nullptr && table->layout() != nullptr)
					table->layout()->activate();
				QApplication::processEvents();
			};
			failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
				QStringLiteral("multiconvfold"), QStringLiteral("normal"));
			failures += saveGrab(rows[0], outDir, skinId, mode,
				QStringLiteral("multiconvfold"), QStringLiteral("normal")) ? 0 : 1;
			// MultiConvolution rebuilds its routing view after file metadata
			// and device channels arrive. The superseded view is hidden and
			// deleteLater'd, but processEvents does not guarantee deferred
			// deletion here; choose the live visible child explicitly.
			RoutingView* view = nullptr;
			for (RoutingView* candidate : rows[0]->findChildren<RoutingView*>())
				if (candidate->isVisible())
					view = candidate;
			if (view == nullptr)
			{
				qWarning("SkinGallery: MultiConvolution fold scene has no routing view (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures++;
			}
			else
			{
				view->galleryShowcase(QStringLiteral("expanded"));
				settle();
				failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
					QStringLiteral("multiconvfold"), QStringLiteral("expanded"));
				failures += saveGrab(rows[0], outDir, skinId, mode,
					QStringLiteral("multiconvfold"), QStringLiteral("expanded")) ? 0 : 1;
			}
		}
	}

	// The dynamic-commands logic block (If/ElseIf/Else/EndIf/Eval), captured
	// as one whole-table shot so the scope presentation that spans rows
	// (rails, brackets, the relay bus) is judged in context. The offscreen
	// gallery runs no analysis, so the engine load facts every skin's
	// presentation reads (branch lamps, TRUE/FALSE readouts, cancelled rows)
	// are injected synthetically: the outer If is taken, the nested If is
	// false, the ElseIf chain is short-circuited and the Else is dead.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, configPath, {
			QStringLiteral("Eval: bassBoost = 6"),
			QStringLiteral("If: outputChannelCount >= 6"),
			QStringLiteral("Preamp: -3 dB"),
			QStringLiteral("If: sampleRate > 48000"),
			QStringLiteral("Delay: 0.25 ms"),
			QStringLiteral("EndIf:"),
			QStringLiteral("ElseIf: outputChannelCount == 4"),
			QStringLiteral("Preamp: -1.5 dB"),
			QStringLiteral("Else:"),
			QStringLiteral("Preamp: 0 dB"),
			QStringLiteral("EndIf:"),
			QStringLiteral("Delay: 5 ms")
		});
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("SkinGallery: logic scene has no table (%s %s)", qPrintable(skinId), qPrintable(mode));
			failures += 1;
		}
		else
		{
			auto fact = [](int line, ConfigLoadTraceEntry::Kind kind, ConfigLoadTraceEntry::Result result,
				bool active, const wchar_t* text = L"") {
				ConfigLoadTraceEntry entry;
				entry.line = line;
				entry.kind = kind;
				entry.result = result;
				entry.active = active;
				entry.text = text;
				return entry;
			};
			table->setLoadTraceFacts({
				fact(1, ConfigLoadTraceEntry::Kind::Eval, ConfigLoadTraceEntry::Result::NotEvaluated, false, L"6"),
				fact(2, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::True, true),
				fact(4, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::False, false),
				fact(5, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(7, ConfigLoadTraceEntry::Kind::Condition, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(8, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(9, ConfigLoadTraceEntry::Kind::ElseBranch, ConfigLoadTraceEntry::Result::NotEvaluated, false),
				fact(10, ConfigLoadTraceEntry::Kind::SkippedLine, ConfigLoadTraceEntry::Result::NotEvaluated, false)
			});
			QApplication::processEvents();
			failures += saveGrab(table, outDir, skinId, mode, QStringLiteral("logic"), QStringLiteral("normal")) ? 0 : 1;
		}
	}

	// A line whose filter could not be set up (a plug-in that does not load),
	// so the engine rolled the whole configuration back. The fact is injected
	// the way the logic scene injects its facts. No skin paints it yet: the row
	// carries it only as a tooltip, so this shot pins the row as it looks today.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 240);
		buildRows(scrollArea, configPath, {
			QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71"),
			QStringLiteral("VSTPlugin: Library missing.dll")
		});
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("SkinGallery: setup error scene has no table (%s %s)", qPrintable(skinId), qPrintable(mode));
			failures += 1;
		}
		else
		{
			ConfigLoadTraceEntry setupError;
			setupError.line = 2;
			setupError.kind = ConfigLoadTraceEntry::Kind::SetupError;
			setupError.error = true;
			setupError.text = L"could not be set up (test), so the configuration was not applied";
			table->setLoadTraceFacts({setupError});
			QApplication::processEvents();
			failures += saveGrab(table, outDir, skinId, mode, QStringLiteral("setuperror"), QStringLiteral("normal")) ? 0 : 1;
		}
	}

	// A Channel: selection group, captured as one whole-table shot: member
	// rows inherit the selection's badges (channel identity on every member,
	// where the rail only shows extent), the Copy member keeps its own
	// destination badges, and Channel: ALL returns the tail to unbadged.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 560);
		buildRows(scrollArea, configPath, {
			QStringLiteral("Channel: L R"),
			QStringLiteral("Filter 1: ON PK Fc 1000 Hz Gain 6 dB Q 0.71"),
			QStringLiteral("Delay: 5 ms"),
			QStringLiteral("Copy: SL=L SR=R"),
			QStringLiteral("Channel: ALL"),
			QStringLiteral("Preamp: -3 dB")
		});
		FilterTable* table = qobject_cast<FilterTable*>(scrollArea.widget());
		if (table == nullptr)
		{
			qWarning("SkinGallery: channel scope scene has no table (%s %s)", qPrintable(skinId), qPrintable(mode));
			failures += 1;
		}
		else
		{
			QApplication::processEvents();
			failures += saveGrab(table, outDir, skinId, mode, QStringLiteral("channelscope"), QStringLiteral("normal")) ? 0 : 1;
		}
	}

	// The expanded state is part of the card contract: all expert controls
	// remain reachable without changing the compact default presentation.
	{
		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		const QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, {
			QStringLiteral("Velvet: Mode=Dynamic Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136")
		});
		if (rows.size() != 1)
		{
			qWarning("SkinGallery: Velvet advanced scene has no row (%s %s)",
				qPrintable(skinId), qPrintable(mode));
			failures++;
		}
		else
		{
			QToolButton* toggle = rows[0]->findChild<QToolButton*>(QStringLiteral("VelvetAdvancedToggle"));
			if (toggle == nullptr)
			{
				qWarning("SkinGallery: Velvet advanced toggle missing (%s %s)",
					qPrintable(skinId), qPrintable(mode));
				failures++;
			}
			else
			{
				toggle->setChecked(true);
				QApplication::processEvents();
				if (rows[0]->layout() != nullptr)
					rows[0]->layout()->activate();
				QApplication::processEvents();
				failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
					QStringLiteral("velvet-advanced"), QStringLiteral("normal"));
				failures += saveGrab(rows[0], outDir, skinId, mode,
					QStringLiteral("velvet-advanced"), QStringLiteral("normal")) ? 0 : 1;
			}
		}
	}

	// A compact dock width catches accidental horizontal minimums in the
	// parameter row. The primary controls must wrap/contract without forcing a
	// horizontal scrollbar; advanced controls stay folded.
	{
		QScrollArea scrollArea;
		scrollArea.resize(520, 640);
		const QList<FilterCardRow*> rows = buildRows(scrollArea, configPath, {
			QStringLiteral("Velvet: Mode=Dynamic Amount=100% Length=27.5625ms Density=1088.435/s Evolution=5s Transition=250ms Decay=-60dB Variation=2050083136")
		});
		if (rows.size() != 1)
		{
			qWarning("SkinGallery: Velvet narrow scene has no row (%s %s)",
				qPrintable(skinId), qPrintable(mode));
			failures++;
		}
		else
		{
			failures += assertNoHorizontalScrollBar(rows[0], skinId, mode,
				QStringLiteral("velvet-narrow"), QStringLiteral("normal"));
			failures += saveGrab(rows[0], outDir, skinId, mode,
				QStringLiteral("velvet-narrow"), QStringLiteral("normal")) ? 0 : 1;
		}
	}
	return failures;
}
}

namespace SkinGallery
{
// Heritage (legacy rows) verification: the mode is a single unskinned
// presentation, so instead of the per-skin per-row matrix it renders two
// whole-table dumps (active and commented rows) for eyeball regression checks.
// Triggered by EAPO_GALLERY_LEGACY=1, used by the local runner scripts.
int renderHeritage(const QDir& outDir, const QString& configPath)
{
	SkinManager::instance()->applyHeritage();

	int failures = 0;
	for (int commented = 0; commented <= 1; commented++)
	{
		QList<QString> lines;
		for (const GalleryRow& row : galleryRows())
			lines.append(commented ? QStringLiteral("# ") + row.line : row.line);

		QScrollArea scrollArea;
		scrollArea.resize(960, 720);
		buildRows(scrollArea, configPath, lines);
		scrollArea.show();
		QCoreApplication::processEvents();

		QPixmap dump = scrollArea.widget()->grab();
		const QString fileName = outDir.filePath(QStringLiteral("heritage_%1.png")
				.arg(commented ? QStringLiteral("disabled") : QStringLiteral("normal")));
		if (dump.isNull() || !dump.save(fileName))
		{
			qWarning("SkinGallery: could not write %s", qPrintable(fileName));
			failures++;
		}
	}
	return failures;
}

int run(const QStringList& arguments)
{
	const int flagIndex = arguments.indexOf(QStringLiteral("--skin-gallery"));
	if (flagIndex < 0 || flagIndex + 1 >= arguments.size())
	{
		qWarning("Usage: Editor --skin-gallery <outDir> [--skin-gallery-skins id,id,...]");
		return 2;
	}

	QDir outDir(arguments.at(flagIndex + 1));
	if (!outDir.mkpath(QStringLiteral(".")))
	{
		qWarning("SkinGallery: cannot create output directory %s", qPrintable(outDir.absolutePath()));
		return 2;
	}

	QStringList skinIds;
	const int skinsIndex = arguments.indexOf(QStringLiteral("--skin-gallery-skins"));
	if (skinsIndex >= 0 && skinsIndex + 1 < arguments.size())
	{
		skinIds = arguments.at(skinsIndex + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
	}
	else
	{
		for (ISkin* skin : Skins::all())
			skinIds.append(skin->id());
	}

	// The gallery renders untranslated English by default (deterministic
	// output for the pixel gates). EAPO_GALLERY_LANG installs a shipped
	// catalog instead, for judging translated typography - the type-scale
	// round asked how the Korean strings read under the new sizes.
	const QByteArray galleryLang = qgetenv("EAPO_GALLERY_LANG").trimmed();
	if (!galleryLang.isEmpty())
	{
		QTranslator* translator = new QTranslator(qApp);
		if (translator->load(QStringLiteral(":/translations/Editor_") + QString::fromLatin1(galleryLang)))
			QCoreApplication::installTranslator(translator);
		else
			qWarning("SkinGallery: no catalog for EAPO_GALLERY_LANG=%s", galleryLang.constData());
	}

	// The reference cards probe target files; the gallery provides synthetic
	// ones and marks itself so the cards skip the audio-service ACL probe,
	// which has no meaningful answer for freshly written scratch files.
	qputenv("EAPO_SKIN_GALLERY", "1");
	QDir fixtureRoot = galleryFixtureRoot();
	if (fixtureRoot.exists() && !fixtureRoot.removeRecursively())
	{
		qWarning("SkinGallery: cannot clear the fixture directory %s", qPrintable(fixtureRoot.absolutePath()));
		return 2;
	}
	const QString configPath = fixtureRoot.mkpath(QStringLiteral(".")) ? buildReferenceFiles(fixtureRoot) : QString();
	if (configPath.isEmpty())
	{
		qWarning("SkinGallery: cannot write reference target files under %s", qPrintable(fixtureRoot.absolutePath()));
		return 2;
	}

	int failures = 0;
	if (qEnvironmentVariableIsSet("EAPO_GALLERY_LEGACY"))
	{
		// Heritage mode is skin-independent; render its two dumps and exit
		// through the same no-teardown path below.
		failures += renderHeritage(outDir, configPath);
		const int status = failures == 0 ? 0 : 1;
		std::fflush(nullptr);
		std::_Exit(status);
	}
	for (const QString& skinId : skinIds)
	{
		failures += renderSkin(outDir, skinId.trimmed(), configPath, true);
		failures += renderSkin(outDir, skinId.trimmed(), configPath, false);
	}

	// Self-check the shot count so a silently dropped skin, row or state fails
	// the run even when every attempted grab succeeded. galleryRows() drives the
	// row term, so adding a gallery row updates this expectation automatically
	// and no external (build.yml) count needs to be touched.
	const int extraShots = fixedScenarioShotCount();
	const int perSkinMode = static_cast<int>(galleryRows().size()) * kStatesPerRow + extraShots;
	const int expected = static_cast<int>(skinIds.size()) * 2 * perSkinMode;
	const int actual = static_cast<int>(outDir.entryList(QStringList{QStringLiteral("*.png")}, QDir::Files).size());
	if (actual != expected)
	{
		qWarning("SkinGallery: expected %d shots (%d skins x 2 modes x (%d rows x %d + %d extras)), wrote %d",
			expected, static_cast<int>(skinIds.size()), static_cast<int>(galleryRows().size()), kStatesPerRow, extraShots, actual);
		failures++;
	}
	for (const QString& skinId : skinIds)
	{
		for (const QString& mode : { QStringLiteral("dark"), QStringLiteral("light") })
		{
			for (const GalleryScenario& scenario : galleryScenarios())
			{
				for (const QString& state : scenario.states)
				{
					const QString fileName = QStringLiteral("%1_%2_%3_%4.png")
						.arg(skinId.trimmed(), mode, scenario.id, state);
					if (!outDir.exists(fileName))
					{
						qWarning("SkinGallery: registered scenario output is missing: %s",
							qPrintable(fileName));
						failures++;
					}
				}
			}
		}
	}

	// The scenes this gallery knows to differ between two runs of the same
	// build, one "<file name>: <reason>" per line, so a pixel comparison
	// (tools/Compare-SkinGallery.ps1) can tell known noise from a regression.
	// The list is written even when empty: its presence says it was checked.
	{
		QFile list(outDir.filePath(QStringLiteral("nondeterministic.txt")));
		if (!list.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		{
			qWarning("SkinGallery: could not write nondeterministic.txt");
			failures++;
		}
		else if (!skinIds.isEmpty())
		{
			// renderSkin() renders dark first, so the first skin's dark dialog is
			// the first QFileDialog the process opens.
			list.write(QStringLiteral("%1_dark_filedialog_normal.png: the first file dialog a run opens "
				"loads the fixture directory asynchronously, so on a busy machine it is sometimes "
				"grabbed with its Back button enabled or before the file rows arrive\n")
				.arg(skinIds.first().trimmed()).toUtf8());
			// Seen only in full runs while the machine was busy: the slope combo
			// boxes of the expanded routing dialog differ by a few pixels.
			bool rackRendered = false;
			for (const QString& id : skinIds)
				rackRendered = rackRendered || id.trimmed() == QStringLiteral("rack");
			if (rackRendered)
			{
				for (const QString& mode : { QStringLiteral("dark"), QStringLiteral("light") })
					list.write(QStringLiteral("rack_%1_srdialog_expanded.png: the slope combo boxes of "
						"the expanded routing dialog sometimes differ by a few pixels when the machine "
						"is busy\n").arg(mode).toUtf8());
			}
		}
	}

	// --skin-gallery is a headless one-shot: by this point every screenshot has
	// been rendered and flushed to disk. Returning normally would unwind into the
	// QApplication / global teardown, which on the offscreen platform never
	// finishes - a leftover background resource keeps the process alive, so the
	// renders all succeed but the process hangs on exit and any driving script
	// has to time out and kill it. Nothing is left to persist, so flush the
	// diagnostic stream and exit immediately with the failure status instead.
	const int status = failures == 0 ? 0 : 1;
	std::fflush(nullptr);
	std::_Exit(status);
}
}
