/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The --knob-specimen diagnostic.
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

// ---------------------------------------------------------------------------
// --knob-specimen <outDir> [--knob-specimen-skins id,id,...] (diagnostic):
// paints a skin's knob straight through ISkin::paintKnob for a fixed set of
// states - card knobs with a figure and row dials without one; hovered,
// dragged, focused, disabled; and squeezed to the 50px a crowded row leaves
// them - at 3x and 1x, dark and light. No widgets are involved, so the
// states can be staged exactly: the gallery cannot stage a drag, and it
// only ever shows the BiQuad dials folded. Judging material for knob rework
// (the minimal drum, issue #338), not a gate. Defaults to minimal.
// ---------------------------------------------------------------------------

#include <QImage>

namespace
{
struct KnobSpecimenCell
{
	QString label;
	KnobState state;
	QSize size;
};

KnobState knobSpecimenState(double ratio, bool bipolar, const QString& text)
{
	KnobState state;
	state.minimum = 0;
	state.maximum = 1000;
	state.value = qRound(ratio * 1000.0);
	state.ratio = ratio;
	state.bipolar = bipolar;
	state.valueText = text;
	return state;
}

void appendKnobSpecimenStates(QList<KnobSpecimenCell>& cells, const KnobState& base, const QSize& size)
{
	KnobState hovered = base;
	hovered.hovered = true;
	cells.append({ QStringLiteral("hover"), hovered, size });
	KnobState dragging = hovered;
	dragging.dragging = true;
	cells.append({ QStringLiteral("drag"), dragging, size });
	KnobState focused = base;
	focused.focused = true;
	cells.append({ QStringLiteral("focus"), focused, size });
	KnobState disabled = base;
	disabled.enabled = false;
	cells.append({ QStringLiteral("disabled"), disabled, size });
}

QList<KnobSpecimenCell> knobSpecimenCells(bool dials)
{
	QList<KnobSpecimenCell> cells;
	if (!dials)
	{
		const QSize size(74, 74);
		const QSize narrow(50, 74);
		const KnobState preamp = knobSpecimenState(0.35, true, QStringLiteral("-6.0"));
		cells.append({ QStringLiteral("preamp -6"), preamp, size });
		cells.append({ QStringLiteral("gain 0 detent"), knobSpecimenState(0.5, true, QStringLiteral("0.0")), size });
		cells.append({ QStringLiteral("delay 0"), knobSpecimenState(0.0, false, QStringLiteral("0.00")), size });
		cells.append({ QStringLiteral("bw 1.000"), knobSpecimenState(0.5, false, QStringLiteral("1.000")), size });
		cells.append({ QStringLiteral("long -100.0"), knobSpecimenState(0.0, true, QStringLiteral("-100.0")), size });
		cells.append({ QStringLiteral("narrow 1.000"), knobSpecimenState(0.5, false, QStringLiteral("1.000")), narrow });
		cells.append({ QStringLiteral("narrow 20000"), knobSpecimenState(1.0, false, QStringLiteral("20000")), narrow });
		appendKnobSpecimenStates(cells, preamp, size);
		return cells;
	}
	const QSize size(84, 66);
	const QSize narrow(50, 66);
	const KnobState frequency = knobSpecimenState(0.566, false, QString());
	cells.append({ QStringLiteral("freq 1 kHz"), frequency, size });
	cells.append({ QStringLiteral("gain +6"), knobSpecimenState(0.65, true, QString()), size });
	cells.append({ QStringLiteral("gain 0"), knobSpecimenState(0.5, true, QString()), size });
	cells.append({ QStringLiteral("q 0.71"), knobSpecimenState(0.38, false, QString()), size });
	cells.append({ QStringLiteral("narrow freq"), frequency, narrow });
	appendKnobSpecimenStates(cells, knobSpecimenState(0.65, true, QString()), size);
	return cells;
}

bool renderKnobSpecimen(const QDir& outDir, const QString& skinId, bool dark, bool dials, int scale)
{
	const QList<KnobSpecimenCell> cells = knobSpecimenCells(dials);
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const int headerHeight = 26;
	const int gap = 10;
	int cellWidth = 0;
	int cellHeight = 0;
	for (const KnobSpecimenCell& cell : cells)
	{
		cellWidth = qMax(cellWidth, cell.size.width() * scale);
		cellHeight = qMax(cellHeight, cell.size.height() * scale);
	}
	QImage image(gap + cells.size() * (cellWidth + gap), headerHeight + cellHeight + gap,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(tokens.background));
	QPainter painter(&image);
	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(9.0);
	painter.setFont(labelFont);
	for (int c = 0; c < cells.size(); c++)
	{
		const int left = gap + c * (cellWidth + gap);
		painter.setPen(QColor(tokens.mutedText));
		painter.drawText(QRect(left, 0, cellWidth, headerHeight), Qt::AlignCenter, cells[c].label);
		painter.save();
		painter.translate(left, headerHeight);
		painter.fillRect(QRect(0, 0, cells[c].size.width() * scale, cells[c].size.height() * scale), QColor(tokens.card));
		painter.scale(scale, scale);
		SkinManager::instance()->paintKnob(painter, QRect(QPoint(0, 0), cells[c].size), cells[c].state);
		painter.restore();
	}
	painter.end();
	const QString name = QStringLiteral("specimen_%1_%2_%3_x%4.png")
		.arg(skinId, dark ? QStringLiteral("dark") : QStringLiteral("light"),
			dials ? QStringLiteral("dials") : QStringLiteral("scalar"))
		.arg(scale);
	if (!image.save(outDir.filePath(name)))
	{
		qWarning("KnobSpecimen: cannot save %s", qPrintable(outDir.filePath(name)));
		return false;
	}
	return true;
}
}

int SkinGallery::runKnobSpecimen(const QStringList& arguments)
{
	const int flagIndex = arguments.indexOf(QStringLiteral("--knob-specimen"));
	if (flagIndex < 0 || flagIndex + 1 >= arguments.size())
	{
		qWarning("Usage: Editor --knob-specimen <outDir> [--knob-specimen-skins id,id,...]");
		return 2;
	}
	QDir outDir(arguments.at(flagIndex + 1));
	if (!outDir.mkpath(QStringLiteral(".")))
	{
		qWarning("KnobSpecimen: cannot create output directory %s", qPrintable(outDir.absolutePath()));
		return 2;
	}
	QStringList skins = { QStringLiteral("minimal") };
	const int skinsIndex = arguments.indexOf(QStringLiteral("--knob-specimen-skins"));
	if (skinsIndex >= 0 && skinsIndex + 1 < arguments.size())
		skins = arguments.at(skinsIndex + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
	int failures = 0;
	for (const QString& skinId : skins)
	{
		for (bool dark : { true, false })
		{
			SkinManager::instance()->applySkin(skinId, dark);
			for (bool dials : { false, true })
				for (int scale : { 3, 1 })
					failures += renderKnobSpecimen(outDir, skinId, dark, dials, scale) ? 0 : 1;
		}
	}
	qWarning("KnobSpecimen: %d failures", failures);
	return failures == 0 ? 0 : 1;
}
