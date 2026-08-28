/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"

void StudioSkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	const bool lit = state.enabled;
	const QRectF plot = state.plotRect;

	QPainterStateGuard painterState(&painter);
	if (!lit)
		painter.setOpacity(0.45);

	// Sunken pane: deep ground behind the one 8px round.
	const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	QPainterPath pane;
	pane.addRoundedRect(frame, 8.0, 8.0);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillPath(pane, QColor(tokens.graph));
	painter.setClipPath(pane);

	// Grid: crisp 1px lines held far behind the data.
	painter.setRenderHint(QPainter::Antialiasing, false);
	const QColor gridMinor = withAlpha(tokens.graphGridMinor, dark ? 84 : 150);
	const QColor gridMajor = withAlpha(tokens.graphGridMajor, dark ? 118 : 165);
	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		const int x = int(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(x, int(plot.top()), x, int(plot.bottom()));
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		const int y = int(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(int(plot.left()), y, int(plot.right()), y);
	}

	// Margin labels: the in-between ticks' labels recede one step
	// further than the majors.
	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(8.5);
	painter.setFont(labelFont);
	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(tokens.mutedText, line.major ? 215 : 140));
		painter.drawText(QRect(int(line.pos) - 24, int(plot.bottom()) + 2, 48,
			state.rect.bottom() - int(plot.bottom()) - 2),
			Qt::AlignHCenter | Qt::AlignTop, line.label);
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(tokens.mutedText, line.major ? 215 : 140));
		painter.drawText(QRect(state.rect.left(), int(line.pos) - 8,
			int(plot.left()) - state.rect.left() - 5, 16),
			Qt::AlignRight | Qt::AlignVCenter, line.label);
	}

	// 0 dB: the knob's luminous anchor laid flat - accent bloom first,
	// text-ink core on top. When the light is off the anchor drops to a
	// quiet muted line.
	if (state.zeroY >= plot.top() && state.zeroY <= plot.bottom())
	{
		const int y = int(state.zeroY);
		if (lit)
		{
			painter.setPen(QPen(withAlpha(tokens.accent, 52), 3));
			painter.drawLine(int(plot.left()), y, int(plot.right()), y);
			painter.setPen(QPen(withAlpha(tokens.text, 200), 1));
		}
		else
		{
			painter.setPen(QPen(withAlpha(tokens.mutedText, 170), 1));
		}
		painter.drawLine(int(plot.left()), y, int(plot.right()), y);
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	const double base = qBound(plot.top(), state.zeroY, plot.bottom());

	if (state.curve.size() >= 2)
	{
		if (lit)
		{
			// The fill sinks from the curve toward the 0 dB line and
			// dies as it lands: a vertical gradient whose alpha peaks
			// away from the baseline on both sides.
			QPolygonF fill = state.curve;
			fill.append(QPointF(state.curve.last().x(), base));
			fill.prepend(QPointF(state.curve.first().x(), base));
			const double zeroRatio = qBound(0.02, (base - plot.top()) / qMax(1.0, plot.height()), 0.98);
			QLinearGradient sink(0, plot.top(), 0, plot.bottom());
			sink.setColorAt(0.0, withAlpha(tokens.accent, 52));
			sink.setColorAt(zeroRatio, withAlpha(tokens.accent, 7));
			sink.setColorAt(1.0, withAlpha(tokens.accent, 44));
			painter.setPen(Qt::NoPen);
			painter.setBrush(sink);
			painter.drawPolygon(fill);
		}

		// The curve: four layered strokes, wide and faint to narrow and
		// full. Lights-out keeps one thin stroke - the data survives,
		// the glow does not.
		painter.setBrush(Qt::NoBrush);
		const struct { double width; int alpha; } layers[] = {
			{ 9.0, 22 },
			{ 5.5, 48 },
			{ 3.0, 110 },
			{ 1.6, 255 }
		};
		for (const auto& layer : layers)
		{
			if (!lit && layer.width > 1.6)
				continue;
			QPen glow(withAlpha(tokens.accent, lit ? layer.alpha : 150), layer.width);
			glow.setCapStyle(Qt::RoundCap);
			glow.setJoinStyle(Qt::RoundJoin);
			painter.setPen(glow);
			painter.drawPolyline(state.curve);
		}
	}

	// Band-locked layouts: light stems rising from the baseline to each
	// band level - bloom under core.
	if (state.bandLocked)
	{
		for (const QPointF& node : state.nodePositions)
		{
			if (lit)
			{
				painter.setPen(QPen(withAlpha(tokens.accent, 36), 4.0));
				painter.drawLine(QPointF(node.x(), base), node);
				painter.setPen(QPen(withAlpha(tokens.accent, 150), 1.6));
			}
			else
			{
				painter.setPen(QPen(withAlpha(tokens.accent, 90), 1.2));
			}
			painter.drawLine(QPointF(node.x(), base), node);
		}
	}

	// Nodes: indicator dots on the luminance ladder; selection adds an
	// outer bloom, keyboard focus a thin ring.
	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const QPointF& center = state.nodePositions.at(i);
		const bool selected = state.selectedNodes.contains(i);
		const bool hovered = state.hoveredNode == i;
		if (lit)
		{
			painter.setPen(Qt::NoPen);
			if (selected)
			{
				painter.setBrush(withAlpha(tokens.accent, 40));
				painter.drawEllipse(center, 9.0, 9.0);
			}
			painter.setBrush(withAlpha(tokens.accent, selected ? 120 : (hovered ? 88 : 36)));
			painter.drawEllipse(center, 6.0, 6.0);
			painter.setBrush(QColor(tokens.accent));
			painter.drawEllipse(center, 3.0, 3.0);
		}
		else
		{
			painter.setPen(QPen(withAlpha(tokens.border, 220), 1.0));
			painter.setBrush(QColor(tokens.card));
			painter.drawEllipse(center, 2.8, 2.8);
		}
		if (lit && state.focused && state.focusedNode == i)
		{
			painter.setPen(QPen(withAlpha(tokens.accent, 110), 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(center, 8.5, 8.5);
		}
	}

	// Cursor readout: dim mono on the glass, alive only under the
	// pointer.
	if (lit && state.cursorValid && !state.cursorText.isEmpty())
	{
		painter.setFont(labelFont);
		painter.setPen(withAlpha(tokens.mutedText, 220));
		painter.drawText(QRectF(plot.adjusted(0, 3, -8, 0)), Qt::AlignRight | Qt::AlignTop, state.cursorText);
	}

	// The pane's edge: a hairline border (the focus ring when the
	// keyboard holds the plot) over a darker inner top edge.
	painter.setClipping(false);
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(state.focused && lit ? QColor(tokens.focusRing) : withAlpha(tokens.border, lit ? 255 : 150), 1.0));
	painter.drawRoundedRect(frame, 8.0, 8.0);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.fillRect(QRectF(frame.left() + 7.0, frame.top() + 1.0, frame.width() - 14.0, 1.0),
		dark ? QColor(0, 0, 0, lit ? 140 : 80) : QColor(0, 0, 0, lit ? 30 : 16));
}
