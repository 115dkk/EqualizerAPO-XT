/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio (glass) skin's routing renderer: "Light Trace" - an etched circuit
	of light on the card glass. Input channels are lit glass chips on the top
	row, outputs on the bottom row, and every connection is a glowing cubic
	trace of the skin's single accent light (glow faked with layered strokes,
	per the constitution's no-effects law). Channel identity stays ink-only;
	state (rest/hover/selected/disabled) is a luminance ladder, never a second
	hue. Replaces the legacy CopyFilterGUIScene reuse (opaque candy pills,
	dead black wiring) that never spoke this skin's language. The working
	state lives in StudioRoutingModel (widget-free, EditorLogicTests-pinned);
	this class owns geometry, painting and interaction only.
*/

#pragma once

#include <QLineEdit>
#include <QPainterPath>
#include <QSet>
#include <QStringList>
#include <QVector>

#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "Editor/widgets/routing/StudioRoutingModel.h"

class StudioRoutingView : public RoutingView
{
	Q_OBJECT

public:
	StudioRoutingView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent);

	std::vector<Assignment> assignments() const override;
	void galleryShowcase(const QString& state) override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void changeEvent(QEvent* event) override;

private slots:
	void commitFactorEditor();
	void commitChannelEditor();

private:
	struct TraceShape
	{
		QPainterPath path;   // the visible curve
		QPainterPath hit;    // stroked hit region (10px) plus the label rect
		QRectF labelRect;    // factor readout window; null when not drawn
		QString labelText;
	};

	void relayout();
	QRect chipRect(bool inputRow, int index) const;
	QPointF portPoint(bool inputRow, int index) const;
	int chipAt(const QPoint& pos, bool* inputRow) const;
	int traceAt(const QPoint& pos) const;
	bool chipHasTrace(bool inputRow, int index) const;
	QString chipLabel(bool inputRow, int index) const;
	void openFactorEditor(int trace);
	void openChannelEditor();

	StudioRoutingModel model;
	RoutingPortModel portModel;

	// Channel fold: hidden ports keep their index but get a null rect, so the
	// trace/port bookkeeping stays index-stable while the glass shows only the
	// channels the command involves. Pinned channels stay lit-out (visible)
	// while unconnected.
	bool channelsExpanded = false;
	QStringList pinnedChannels;
	QVector<bool> inputVisible;
	QVector<bool> outputVisible;
	int hiddenOutputs = 0;

	QVector<QRect> inputRects;
	QVector<QRect> outputRects;
	QVector<TraceShape> traceShapes;
	QRect ghostRect;   // the virtual-output entry point; null in no-add states
	QRect revealRect;  // the fold's +N / fold ghost chip on the input row
	QRect removeRect;  // hovered virtual output chip's x target
	int removeChip = -1;

	QSet<int> selectedTraces;
	int hoveredTrace = -1;
	int hoveredChip = -1;
	bool hoveredChipIsInput = false;
	bool ghostHovered = false;
	bool revealHovered = false;

	// Drag-to-connect state.
	int dragChip = -1;
	bool dragFromInput = false;
	bool dragging = false;
	QPoint dragStart;
	QPoint dragPos;

	QLineEdit* factorEditor = nullptr;
	int factorEditorTrace = -1;
	QLineEdit* channelEditor = nullptr;
};

class LightTraceRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent) override;
	const char* id() const override { return "light-trace"; }
};
