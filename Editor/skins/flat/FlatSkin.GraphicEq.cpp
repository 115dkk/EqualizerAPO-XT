/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FlatSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "FlatPaint.h"

// The GraphicEQ response plot, in the analysis graph's grammar: a panel, a
// hairline grid, the response as one line in the emphasis colour, and the
// band points as small handles on it. GraphicEQPlotWidget owns the model and
// every gesture.
void FlatSkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
{
	const QColor card(tokens.card);
	const QColor accent(tokens.accent);
	const QColor muted(tokens.mutedText);
	const QColor line(tokens.border);

	const QRectF frame(state.rect);
	QPainterPath panelPath;
	panelPath.addRoundedRect(frame.adjusted(0.5, 0.5, -0.5, -0.5), FlatPaint::surfaceRadius - 0.5,
		FlatPaint::surfaceRadius - 0.5);

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	// Keyboard focus on the surface is the panel's own line in the emphasis
	// colour, one pixel heavier (shared article 8).
	const bool surfaceFocus = state.focused && state.enabled;
	FlatPaint::box(painter, frame, card, surfaceFocus ? QColor(tokens.focusRing) : line, FlatPaint::surfaceRadius,
		surfaceFocus ? 2.0 : 1.0);

	QPainterStateGuard panelState(&painter);
	painter.setClipPath(panelPath);
	// A switched-off plot fades as a whole; it keeps its grid and its curve.
	if (!state.enabled)
		painter.setOpacity(FlatPaint::disabledOpacity);

	const QFont labelFont = FlatPaint::numberFont(tokens, 12);
	const QFontMetricsF labelMetrics(labelFont);
	painter.setFont(labelFont);

	for (const GraphicEQPlotState::GridLine& gridLine : state.vertical)
	{
		if (!gridLine.major)
			continue;
		painter.fillRect(QRectF(qFloor(gridLine.pos), state.plotRect.top(), 1.0, state.plotRect.height()), line);
		if (!gridLine.label.isEmpty())
		{
			painter.setPen(muted);
			// The window-edge figure (20k) tucks inside the rounding instead
			// of being sliced by the clip.
			QRectF labelRect = skinXTickLabelRect(gridLine.pos, state.plotRect.bottom() + 3.0,
				state.rect.bottom() - state.plotRect.bottom() - 3.0);
			int align = Qt::AlignHCenter;
			if (labelRect.right() > state.rect.right() - 6)
			{
				labelRect.setRight(state.rect.right() - 6);
				align = Qt::AlignRight;
			}
			painter.drawText(labelRect, align | Qt::AlignTop, gridLine.label);
		}
	}
	for (const GraphicEQPlotState::GridLine& gridLine : state.horizontal)
	{
		if (!gridLine.major)
			continue;
		// The 0 dB row is drawn below, one step heavier; skip its grid twin.
		if (qAbs(gridLine.pos - state.zeroY) > 1.0)
			painter.fillRect(QRectF(state.plotRect.left(), qFloor(gridLine.pos), state.plotRect.width(), 1.0), line);
		if (!gridLine.label.isEmpty())
		{
			painter.setPen(muted);
			painter.drawText(QRectF(state.rect.left() + 2.0, gridLine.pos - 8.0,
				state.plotRect.left() - state.rect.left() - 8.0, 16.0),
				Qt::AlignRight | Qt::AlignVCenter, gridLine.label);
		}
	}
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
		painter.fillRect(QRectF(state.plotRect.left(), qFloor(state.zeroY), state.plotRect.width(), 1.0),
			QColor(tokens.controlEdge));

	// 15/31-band layouts read as levels on fixed bands: a thin stem from the
	// 0 dB row to each band point, under the curve.
	if (state.bandLocked)
	{
		const double base = state.clampedZeroY();
		painter.setPen(QPen(QColor(tokens.borderStrong), 2.0, Qt::SolidLine, Qt::FlatCap));
		for (const QPointF& node : state.nodePositions)
			painter.drawLine(QPointF(node.x(), base), node);
	}

	if (state.curve.size() >= 2)
	{
		painter.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(state.curve);
	}

	// The band points: the card's surface inside a ring of the emphasis
	// colour at rest, filled when chosen. Hover takes the neutral step and
	// half a pixel; the keyboard's point takes one pixel more on its ring.
	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const QPointF& center = state.nodePositions.at(i);
		const bool selected = state.selectedNodes.contains(i);
		const bool hovered = state.enabled && state.hoveredNode == i;
		const bool keyTarget = state.enabled && state.focused && state.focusedNode == i;
		const double radius = hovered ? 5.5 : 5.0;
		painter.setPen(QPen(accent, keyTarget ? 3.0 : 2.0));
		if (selected)
			painter.setBrush(accent);
		else
			painter.setBrush(hovered ? QColor(tokens.cardHover) : card);
		painter.drawEllipse(center, radius, radius);
	}

	// The cursor's reading, as a tag in the top-right corner.
	if (state.enabled && state.cursorValid && !state.cursorText.isEmpty())
	{
		const qreal tagHeight = 22.0;
		const qreal tagWidth = labelMetrics.horizontalAdvance(state.cursorText) + 18.0;
		const QRectF tag(state.plotRect.right() - tagWidth - 6.0, state.plotRect.top() + 4.0, tagWidth, tagHeight);
		FlatPaint::box(painter, tag, card, QColor(tokens.borderStrong), tagHeight / 2.0);
		painter.setFont(labelFont);
		painter.setPen(QColor(tokens.text));
		painter.drawText(tag, Qt::AlignCenter, state.cursorText);
	}
}
