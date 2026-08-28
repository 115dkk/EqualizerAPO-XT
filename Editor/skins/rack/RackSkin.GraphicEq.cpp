/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

void RackSkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	const bool powered = state.enabled;
	QPainterStateGuard painterState(&painter);

	// The scope well is dark in BOTH finishes (the display law). The
	// graticule sits in the scope-grid family: the cream table's grid token
	// is panel paint, so it never reaches the glass.
	const QColor glassTop = dark ? QColor(0x04, 0x06, 0x05) : QColor(0x0A, 0x0E, 0x0B);
	const QColor glassBottom = dark ? QColor(0x0A, 0x0F, 0x0C) : QColor(0x11, 0x16, 0x10);
	const QColor bezel = dark ? QColor(0x05, 0x08, 0x07) : QColor(0x4A, 0x44, 0x38);
	const QColor bezelLip = dark ? QColor(0x39, 0x42, 0x4A) : QColor(0x6B, 0x63, 0x54);
	const QColor gridMinor = dark ? QColor(tokens.graphGridMinor) : QColor(0x25, 0x43, 0x37);
	const QColor gridMajor = gridMinor.lighter(168);
	// Phosphor: accent2 is the machine's LED green. The cream panel's token
	// is paint, not light, so it is lifted to emission strength on the glass.
	const QColor phosphor = dark ? QColor(tokens.accent2) : QColor(tokens.accent2).lighter(195);
	// Etched axis figures and the cursor readout follow the sheets' LCD
	// segment palette; a powered-down display dims to the service shade.
	const QColor segmentBright = dark ? QColor(0x86, 0xF2, 0xBA) : QColor(0x3E, 0xD6, 0x8E);
	const QColor segmentDim = dark ? QColor(0x4C, 0x9E, 0x74) : QColor(0x2F, 0x8A, 0x61);
	const QColor segmentOff = dark ? QColor(0x3A, 0x6B, 0x51) : QColor(0x2F, 0x6B, 0x4D);

	const QRectF r = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = 2.0;
	QPainterPath glass;
	glass.addRoundedRect(r, radius, radius);
	painter.setClipPath(glass);

	// Glass ground: the tube face, slightly deeper at the top under the
	// bezel's overhang.
	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient ground(r.topLeft(), r.bottomLeft());
	ground.setColorAt(0.0, glassTop);
	ground.setColorAt(1.0, glassBottom);
	painter.fillRect(r, ground);

	// While powered, the faint memory of the beam warms the middle of the
	// tube (a fill, not an effect).
	if (powered)
	{
		QRadialGradient backGlow(state.plotRect.center(), state.plotRect.width() * 0.55);
		backGlow.setColorAt(0.0, withAlpha(phosphor, dark ? 12 : 14));
		backGlow.setColorAt(1.0, withAlpha(phosphor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(backGlow);
		painter.drawRect(state.plotRect);
	}

	// Graticule: crisp 1px rules - straight lines carry no antialiasing.
	painter.setRenderHint(QPainter::Antialiasing, false);
	const int gridAlpha = powered ? 255 : 150;
	const int plotTop = int(state.plotRect.top());
	const int plotBottom = int(state.plotRect.bottom());
	const int plotLeft = int(state.plotRect.left());
	const int plotRight = int(state.plotRect.right());
	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		painter.setPen(QPen(withAlpha(line.major ? gridMajor : gridMinor, gridAlpha), 1));
		painter.drawLine(int(line.pos), plotTop, int(line.pos), plotBottom);
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		painter.setPen(QPen(withAlpha(line.major ? gridMajor : gridMinor, gridAlpha), 1));
		painter.drawLine(plotLeft, int(line.pos), plotRight, int(line.pos));
	}

	// The 0 dB centre axis: a phosphor-tinted rule with the scope's fine
	// hash marks between the graticule columns.
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
	{
		const int zeroY = int(state.zeroY);
		painter.setPen(QPen(withAlpha(powered ? phosphor : segmentOff, powered ? 145 : 80), 1));
		painter.drawLine(plotLeft, zeroY, plotRight, zeroY);
		painter.setPen(QPen(withAlpha(powered ? phosphor : segmentOff, powered ? 60 : 40), 1));
		for (int x = plotLeft + 4; x < plotRight - 2; x += 7)
			painter.drawLine(x, zeroY - 2, x, zeroY + 2);
	}

	// Axis figures: etched in the glass margins in segment ink (numerals,
	// never translated). Majors read a step brighter than minors.
	QFont axisFont(tokens.monoFontFamily);
	axisFont.setPointSizeF(8.0);
	axisFont.setBold(true);
	painter.setFont(axisFont);
	const QColor axisInk = powered ? segmentDim : segmentOff;
	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(axisInk, line.major ? (powered ? 235 : 170) : (powered ? 140 : 100)));
		painter.drawText(QRect(int(line.pos) - 24, plotBottom + 2, 48, state.rect.bottom() - plotBottom - 2),
			Qt::AlignHCenter | Qt::AlignTop, line.label);
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(axisInk, line.major ? (powered ? 235 : 170) : (powered ? 140 : 100)));
		painter.drawText(QRect(state.rect.left() + 2, int(line.pos) - 8, plotLeft - state.rect.left() - 6, 16),
			Qt::AlignRight | Qt::AlignVCenter, line.label);
	}

	// The beam stays inside the graticule area, like a tube masks its trace.
	QPainterStateGuard beamState(&painter);
	painter.setClipRect(state.plotRect.adjusted(-1, -1, 1, 1), Qt::IntersectClip);

	if (state.curve.size() >= 2)
	{
		const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());

		// Afterglow: a faint phosphor wash between the trace and the axis.
		QPolygonF afterglow = state.curve;
		afterglow.append(QPointF(state.curve.last().x(), base));
		afterglow.prepend(QPointF(state.curve.first().x(), base));
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(phosphor, powered ? 22 : 10));
		painter.drawPolygon(afterglow);

		// The trace: glow faked by stroke overpainting (wide dim passes under
		// the beam core - no graphics effects on this machine). A powered-down
		// display keeps a single dim burned-in trace.
		painter.setBrush(Qt::NoBrush);
		if (powered)
		{
			painter.setPen(QPen(withAlpha(phosphor, 26), 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
			painter.setPen(QPen(withAlpha(phosphor, 70), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
			painter.setPen(QPen(phosphor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
		}
		else
		{
			painter.setPen(QPen(withAlpha(segmentOff, 200), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(state.curve);
		}

		// Band-locked layouts (15/31) read as levels on fixed bands: each
		// node hangs a segmented level ladder off the centre axis, the way a
		// hardware analyzer steps its columns.
		if (state.bandLocked)
		{
			painter.setRenderHint(QPainter::Antialiasing, false);
			painter.setPen(QPen(powered ? withAlpha(phosphor, 110) : withAlpha(segmentOff, 80), 3));
			for (const QPointF& node : state.nodePositions)
			{
				const qreal x = qFloor(node.x()) + 0.5;
				const qreal length = qAbs(node.y() - base);
				const qreal direction = node.y() < base ? -1.0 : 1.0;
				for (qreal offset = 2.0; offset + 3.0 <= length; offset += 5.0)
					painter.drawLine(QPointF(x, base + direction * offset), QPointF(x, base + direction * (offset + 3.0)));
			}
		}
	}

	// Nodes are glowing adjustment dots: rest = a quiet dome, hover = the
	// dome pre-heats, selected = lit core with the adjustment collar ring.
	painter.setRenderHint(QPainter::Antialiasing, true);
	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const QPointF& center = state.nodePositions.at(i);
		const bool selected = state.selectedNodes.contains(i);
		const bool warmed = state.hoveredNode == i;

		if (!powered)
		{
			// Power off: the adjustment points survive as dark domes.
			painter.setPen(QPen(withAlpha(segmentOff, 150), 1));
			painter.setBrush(withAlpha(segmentOff, 60));
			painter.drawEllipse(center, 2.6, 2.6);
			continue;
		}

		const qreal haloRadius = selected ? 9.0 : (warmed ? 8.0 : 5.5);
		QRadialGradient halo(center, haloRadius);
		halo.setColorAt(0.0, withAlpha(phosphor, selected ? 110 : (warmed ? 85 : 45)));
		halo.setColorAt(1.0, withAlpha(phosphor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, haloRadius, haloRadius);

		const QColor core = selected ? phosphor.lighter(145) : (warmed ? phosphor.lighter(118) : phosphor);
		painter.setBrush(withAlpha(core, selected ? 255 : (warmed ? 240 : 205)));
		painter.drawEllipse(center, selected ? 3.2 : 2.7, selected ? 3.2 : 2.7);

		if (selected)
		{
			painter.setPen(QPen(withAlpha(phosphor, 220), 1.2));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(center, 5.4, 5.4);
		}
		// The keyboard target wears the amber service ring while the display
		// holds focus - the machine's focus law reaching into the glass.
		if (state.focused && state.focusedNode == i)
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 200), 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(center, 7.0, 7.0);
		}
	}
	beamState.restore();

	// Cursor readout: top-right inside the glass, bright segments.
	if (powered && state.cursorValid && !state.cursorText.isEmpty())
	{
		QFont readoutFont(tokens.monoFontFamily);
		readoutFont.setPointSizeF(8.5);
		readoutFont.setBold(true);
		painter.setFont(readoutFont);
		painter.setPen(segmentBright);
		painter.drawText(QRectF(state.plotRect.adjusted(0, 3, -8, 0)), Qt::AlignRight | Qt::AlignTop, state.cursorText);
	}

	// The bezel's overhang shadow hangs over the top of the glass - the
	// recessed grammar (shadowed top edge, lit lower lip below).
	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient overhang(r.topLeft(), QPointF(r.left(), r.top() + 9.0));
	overhang.setColorAt(0.0, QColor(0, 0, 0, dark ? 150 : 130));
	overhang.setColorAt(1.0, QColor(0, 0, 0, 0));
	painter.fillRect(QRectF(r.left(), r.top(), r.width(), 9.0), overhang);

	// Bezel frame: the LCD-well border grammar. Focus lights the amber
	// service edge; at rest the bottom border is the lit lower lip.
	painter.setClipping(false);
	painter.setBrush(Qt::NoBrush);
	if (state.focused)
	{
		painter.setPen(QPen(QColor(tokens.focusRing), 1));
		painter.drawRoundedRect(r, radius, radius);
	}
	else
	{
		painter.setPen(QPen(bezel, 1));
		painter.drawRoundedRect(r, radius, radius);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setPen(QPen(bezelLip, 1));
		painter.drawLine(QPointF(r.left() + radius, r.bottom()), QPointF(r.right() - radius, r.bottom()));
	}
}
