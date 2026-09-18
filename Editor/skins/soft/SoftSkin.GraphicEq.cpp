/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

// The GraphicEQ response plot: "the response curve you cannot fear".
// GraphicEQPlotWidget owns the model and every gesture; every pixel
// here is this skin's own instrument.
void SoftSkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
{
	const QColor card(tokens.card);
	const QColor accent(tokens.accent);
	const QColor muted(tokens.mutedText);
	const QColor border(tokens.border);
	const QColor well = state.enabled ? QColor(tokens.surfaceSunken) : QColor(tokens.background);

	QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal wellRound = 14.0;
	QPainterPath wellPath;
	wellPath.addRoundedRect(frame, wellRound, wellRound);

	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(well);
	painter.drawPath(wellPath);

	QPainterStateGuard wellState(&painter);
	painter.setClipPath(wellPath);

	// Axis captions ride the body face in faded ink - the constitution
	// reserves mono for value chips, and these are captions.
	QFont labelFont(tokens.fontFamily);
	labelFont.setPointSizeF(8.5);
	labelFont.setWeight(QFont::DemiBold);
	painter.setFont(labelFont);
	const QColor labelInk = withAlpha(muted, state.enabled ? 210 : 120);

	// Major-only grid, the border sunk most of the way into the well.
	// Straight axis lines stay crisp: antialiasing off. The sleeping slot
	// drops the lines entirely and keeps only the captions - whitespace.
	painter.setRenderHint(QPainter::Antialiasing, false);
	const QColor gridInk = mixColor(border, well, 0.25);
	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		if (!line.major)
			continue;
		const int x = qRound(line.pos);
		if (state.enabled)
		{
			painter.setPen(QPen(gridInk, 1));
			painter.drawLine(x, int(state.plotRect.top()), x, int(state.plotRect.bottom()));
		}
		if (!line.label.isEmpty())
		{
			painter.setPen(labelInk);
			// The window-edge caption (20k) tucks inside the rounding
			// instead of getting sliced by the clip.
			QRect labelRect(x - 24, int(state.plotRect.bottom()) + 3, 48,
				state.rect.bottom() - int(state.plotRect.bottom()) - 3);
			int align = Qt::AlignHCenter;
			if (labelRect.right() > state.rect.right() - 6)
			{
				labelRect.setRight(state.rect.right() - 6);
				align = Qt::AlignRight;
			}
			painter.drawText(labelRect, align | Qt::AlignTop, line.label);
		}
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		if (!line.major)
			continue;
		const int y = qRound(line.pos);
		// The 0 dB row is the soft notch drawn below; skip its grid twin.
		if (state.enabled && qAbs(line.pos - state.zeroY) > 1.0)
		{
			painter.setPen(QPen(gridInk, 1));
			painter.drawLine(int(state.plotRect.left()), y, int(state.plotRect.right()), y);
		}
		if (!line.label.isEmpty())
		{
			painter.setPen(labelInk);
			painter.drawText(QRect(state.rect.left() + 2, y - 8,
				int(state.plotRect.left()) - state.rect.left() - 8, 16),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
		}
	}
	painter.setRenderHint(QPainter::Antialiasing, true);

	// The soft 0 dB notch line.
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.text), state.enabled ? 110 : 55), 2,
			Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(state.plotRect.left() + 6.0, state.zeroY),
			QPointF(state.plotRect.right() - 6.0, state.zeroY));
	}

	if (state.curve.size() >= 2)
	{
		if (!state.enabled)
		{
			// Sleeping: the ghost of the response in muted ink, no pastel.
			painter.setPen(QPen(withAlpha(muted, 120), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(state.curve);
		}
		else
		{
			// One accent line with a faint wash of the same ink down to
			// the notch, either side of 0 dB: boost and cut are directions
			// the notch already tells apart, not hues (the accent2 cut
			// side retired with the concept swap).
			const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
			QPolygonF fillPoly = state.curve;
			fillPoly.append(QPointF(state.curve.last().x(), base));
			fillPoly.prepend(QPointF(state.curve.first().x(), base));
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(accent, 26));
			painter.drawPolygon(fillPoly);
			painter.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(state.curve);
		}
	}

	// 15/31-band layouts read as levels on fixed bands: rounded pastel
	// stems grow from the notch, the console silhouette without bar walls.
	if (state.bandLocked && state.enabled)
	{
		const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
		for (const QPointF& node : state.nodePositions)
		{
			painter.setPen(QPen(mixColor(accent, card, 0.78), 4, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(node.x(), base), node);
		}
	}

	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const QPointF& center = state.nodePositions.at(i);
		const bool selected = state.selectedNodes.contains(i);
		const bool hovered = state.hoveredNode == i;

		if (!state.enabled)
		{
			painter.setPen(QPen(withAlpha(muted, 120), 1.5));
			painter.setBrush(well);
			painter.drawEllipse(center, 4.0, 4.0);
			continue;
		}

		// Rest 5px, half a pixel more on hover - the calmest "you can
		// grab me" cue; precision lives in the readout strip anyway.
		const double radius = hovered ? 5.5 : 5.0;
		if (selected)
		{
			// The chosen-pill grammar, in the round: the tint under a
			// full accent ring.
			painter.setPen(QPen(accent, 2));
			painter.setBrush(QColor(tokens.cardSelected));
		}
		else
		{
			// Resting: the paper face with the accent ring; hover lifts
			// the face exactly one value step.
			painter.setPen(QPen(accent, 1.5));
			painter.setBrush(hovered ? QColor(tokens.cardHover) : card);
		}
		painter.drawEllipse(center, radius, radius);

		// The keyboard's current node announces itself with the quiet
		// halo (selection's own ring already covers a selected one).
		if (state.focusedNode == i && state.focused && !selected)
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(center, radius + 4.0, radius + 4.0);
		}
	}

	// Cursor readout: a chosen pill (tint, accent ink, the mixed edge)
	// resting in the well's top-right corner.
	if (state.enabled && state.cursorValid && !state.cursorText.isEmpty())
	{
		QFont pillFont(tokens.fontFamily);
		pillFont.setPointSizeF(8.5);
		pillFont.setWeight(QFont::DemiBold);
		const QFontMetricsF pillMetrics(pillFont);
		const qreal pillH = 18.0;
		const qreal pillW = pillMetrics.horizontalAdvance(state.cursorText) + 16.0;
		QRectF pill(state.plotRect.right() - pillW - 6.0, state.plotRect.top() + 6.0, pillW, pillH);
		painter.setPen(QPen(mixColor(card, accent, 0.42), 1));
		painter.setBrush(QColor(tokens.cardSelected));
		painter.drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
		painter.setFont(pillFont);
		painter.setPen(accent);
		painter.drawText(pill, Qt::AlignCenter, state.cursorText);
	}

	wellState.restore();

	// The well edge: a very light 1px line awake; asleep it becomes the
	// dashed outline of the sleeping-slot triple.
	QPen edge(border, 1);
	if (!state.enabled)
		edge.setStyle(Qt::DashLine);
	painter.setPen(edge);
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(wellPath);

	// Keyboard focus on the surface itself: the quiet halo hugging the
	// inside of the well, never a hard ring.
	if (state.focused && state.enabled)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
		painter.drawRoundedRect(frame.adjusted(2.0, 2.0, -2.0, -2.0), 12.0, 12.0);
	}
}
