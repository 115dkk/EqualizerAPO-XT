/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

// The GraphicEQ response plot as this skin's instrument: a measurement
// record on the console (dark) or the printed sheet (light). Every
// straight line is drawn crisp with antialiasing off; only the response
// curve keeps its antialiasing, because the curve is data.
void MinimalSkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
{
	const QColor ground(tokens.graph);
	const QColor gridMinor(tokens.graphGridMinor);
	const QColor gridMajor(tokens.graphGridMajor);
	const QColor secondary(tokens.mutedText);
	const QColor bodyInk(state.enabled ? QColor(tokens.text) : QColor(tokens.mutedText));
	const QColor accent(tokens.accent);

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.fillRect(state.rect, ground);

	// Axis labels: secondary-ink mono print in the margins.
	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(8.5);
	painter.setFont(labelFont);

	const int plotLeft = int(state.plotRect.left());
	const int plotRight = int(state.plotRect.right());
	const int plotTop = int(state.plotRect.top());
	const int plotBottom = int(state.plotRect.bottom());

	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		const int x = qRound(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(x, plotTop, x, plotBottom);
		if (!line.label.isEmpty())
		{
			painter.setPen(secondary);
			painter.drawText(QRect(x - 24, plotBottom + 2, 48, state.rect.bottom() - plotBottom - 2),
				Qt::AlignHCenter | Qt::AlignTop, line.label);
		}
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		const int y = qRound(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(plotLeft, y, plotRight, y);
		if (!line.label.isEmpty())
		{
			painter.setPen(secondary);
			painter.drawText(QRect(state.rect.left(), y - 8, plotLeft - state.rect.left() - 4, 16),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
		}
	}

	// The 0 dB rule: the one full-strength straight line, body ink 1px.
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
	{
		painter.setPen(QPen(bodyInk, 1));
		painter.drawLine(plotLeft, qRound(state.zeroY), plotRight, qRound(state.zeroY));
	}

	// Band-locked stems: 1px hairline verticals from the 0 dB rule to each
	// band level, in secondary ink so the response stays the brightest line.
	if (state.bandLocked)
	{
		painter.setPen(QPen(secondary, 1));
		const int base = qRound(qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom()));
		for (const QPointF& node : state.nodePositions)
			painter.drawLine(qRound(node.x()), base, qRound(node.x()), qRound(node.y()));
	}

	// The response: data, so it keeps its antialiasing. 1px, no fill.
	if (state.curve.size() >= 2)
	{
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(QPen(bodyInk, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(state.curve);
		painter.setRenderHint(QPainter::Antialiasing, false);
	}

	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const int x = qRound(state.nodePositions.at(i).x());
		const int y = qRound(state.nodePositions.at(i).y());
		const bool selected = state.selectedNodes.contains(i);
		const bool hovered = state.hoveredNode == i;
		if (selected)
		{
			// The inverted block. While a drag is live the dragged node is the
			// selected one under the pointer, and only it turns accent (the
			// active-state law); a disabled row's stored selection dims to the
			// secondary block - still inverted, no longer a live cursor.
			const QColor block = !state.enabled ? secondary : (hovered ? accent : bodyInk);
			painter.fillRect(QRect(x - 3, y - 3, 7, 7), block);
		}
		else
		{
			// A square hairline tick punched into the ground; hover fills it
			// exactly one background value step.
			painter.setPen(QPen(bodyInk, 1));
			painter.setBrush(hovered ? QColor(tokens.cardHover) : ground);
			painter.drawRect(x - 3, y - 3, 6, 6);
		}
		if (state.focused && state.focusedNode == i)
		{
			// The keyboard cursor: the square accent hairline frame.
			painter.setPen(QPen(QColor(tokens.focusRing), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRect(x - 5, y - 5, 10, 10);
		}
	}

	// Cursor readout: one secondary-ink mono line, top right.
	if (state.cursorValid && !state.cursorText.isEmpty())
	{
		painter.setPen(secondary);
		painter.setFont(labelFont);
		painter.drawText(QRectF(state.plotRect).adjusted(0, 2, -4, 0), Qt::AlignRight | Qt::AlignTop, state.cursorText);
	}

	// The frame: one square 1px hairline, exactly like the analysis graph;
	// keyboard focus swaps it for the accent hairline (focus grammar).
	painter.setPen(QPen(state.focused ? QColor(tokens.focusRing) : QColor(tokens.border), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
}
