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

void StudioSkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	const QRectF plot = state.plotRect;
	const double hover = qBound(0.0, state.hover, 1.0);
	const bool magnitude = state.metric == AnalysisMetric::MagnitudeDb;

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// Sunken pane: the deep graph ground behind the one 8px round.
	const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	QPainterPath pane;
	pane.addRoundedRect(frame, 8.0, 8.0);
	painter.fillPath(pane, QColor(tokens.graph));
	painter.setClipPath(pane);

	// The pane's inner light answers hover. Dark: a frost sheen settling
	// from the top. Light: the thickness shade pooling at the bottom
	// deepens instead (white glass cannot brighten).
	if (dark)
	{
		QLinearGradient sheen(frame.topLeft(), QPointF(frame.left(), frame.top() + frame.height() * 0.45));
		sheen.setColorAt(0.0, QColor(255, 255, 255, qRound(6.0 + 12.0 * hover)));
		sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
		painter.fillPath(pane, sheen);
	}
	else
	{
		QLinearGradient depthShade(QPointF(frame.left(), frame.bottom() - frame.height() * 0.38), frame.bottomLeft());
		depthShade.setColorAt(0.0, QColor(24, 32, 51, 0));
		depthShade.setColorAt(1.0, QColor(24, 32, 51, qRound(18.0 + 8.0 * hover)));
		painter.fillPath(pane, depthShade);
	}

	// Grid: crisp 1px lines held far behind the data.
	painter.setRenderHint(QPainter::Antialiasing, false);
	const QColor gridMinor = withAlpha(tokens.graphGridMinor, dark ? 84 : 150);
	const QColor gridMajor = withAlpha(tokens.graphGridMajor, dark ? 118 : 165);
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(int(line.pos), int(plot.top()), int(line.pos), int(plot.bottom()));
	}
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(int(plot.left()), int(line.pos), int(plot.right()), int(line.pos));
	}

	// Axis figures: minors one step dimmer; frequency figures under
	// their ticks, dB figures inside the pane's left edge. Tight fits
	// shed minor labels first.
	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(8.5);
	painter.setFont(labelFont);
	const int vCount = state.vertical.size();
	const double vSpacing = vCount > 1 ? plot.width() / (vCount - 1) : plot.width();
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		if (line.label.isEmpty() || (!line.major && vSpacing < 30.0))
			continue;
		painter.setPen(withAlpha(tokens.mutedText, line.major ? 215 : 140));
		// int() and toRect() keep the integer box the glass has always used: a
		// float rect lays the glyphs out on subpixels and would move them.
		painter.drawText(skinXTickLabelRect(int(line.pos), int(plot.bottom()) + 2, 11.0).toRect(),
			Qt::AlignHCenter | Qt::AlignTop, line.label);
	}
	const int hCount = state.horizontal.size();
	const double hSpacing = hCount > 1 ? plot.height() / (hCount - 1) : plot.height();
	const int hLabelStep = hSpacing >= 13.0 ? 1 : (hSpacing >= 6.5 ? 2 : 4);
	for (int i = 0; i < hCount; i++)
	{
		const AnalysisGraphState::GridLine& line = state.horizontal.at(i);
		if (line.label.isEmpty() || (!line.major && (i % hLabelStep) != 0))
			continue;
		painter.setPen(withAlpha(tokens.mutedText, line.major ? 200 : 130));
		// Clamped so the topmost and bottommost figures stay inside the glass;
		// the clamp is on the box's centre, which is what the helper takes.
		const double labelY = qBound(plot.top() + 7.0, line.pos - 6.0, plot.bottom() - 7.0);
		painter.drawText(skinYTickLabelRect(labelY, plot.left() + 5.0, 44.0, 10.0),
			Qt::AlignLeft | Qt::AlignVCenter, line.label);
	}

	// Footer caption: the channel/sample-rate readout (localized data,
	// drawn as-is) centred under the tick figures.
	if (!state.channelText.isEmpty())
	{
		const QFontMetricsF captionMetrics(labelFont);
		painter.setPen(withAlpha(tokens.mutedText, 190));
		painter.drawText(QRectF(plot.left(), plot.bottom() + 14.0, plot.width(),
			qMax(0.0, frame.bottom() - plot.bottom() - 14.0)),
			Qt::AlignHCenter | Qt::AlignTop,
			captionMetrics.elidedText(state.channelText, Qt::ElideRight, plot.width()));
	}

	// What the value axis is measuring, engraved in the corner the CLIP
	// chip owns under magnitude - which is free here, because neither
	// other metric can clip. The tick figures are bare signed numbers in
	// every metric, so without this the degree sign and the millisecond
	// never appear; the prepared span text carries both ends and the
	// unit in the metric's own spelling. Drawn before the data, so the
	// trace passes in front of it (the UI recedes behind the values).
	// Magnitude keeps its historical silence: dB is the assumed default,
	// and this pane must not change one pixel of it.
	if (!magnitude && !state.spanValueText.isEmpty())
	{
		const QFontMetricsF spanMetrics(labelFont);
		painter.setPen(withAlpha(tokens.mutedText, 190));
		painter.drawText(QRectF(plot.left(), plot.top() + 4.0, plot.width() - 8.0, 11.0),
			Qt::AlignRight | Qt::AlignTop,
			spanMetrics.elidedText(state.spanValueText, Qt::ElideRight, plot.width() - 8.0));
	}

	// Phase turns: the frequencies where the response has come a half or
	// a whole way around. Magnitude has one landmark (unity) and phase
	// has a ladder of them, so the anchor grammar extends one step down
	// the luminance ladder - accent bloom under an accent core, brighter
	// than the grid and dimmer than the zero anchor, and never the
	// text-ink core that keeps the anchor unmistakable. Drawn only while
	// the turns can still be counted: a phase wound thousands of degrees
	// deep leaves the reading to the grid.
	if (state.metric == AnalysisMetric::PhaseDegrees && state.maximum > state.minimum)
	{
		const double span = state.maximum - state.minimum;
		const double deepest = qMax(qAbs(state.minimum), qAbs(state.maximum));
		if (plot.height() * 180.0 / span >= 18.0)
		{
			for (double turn = 180.0; turn <= deepest; turn += 180.0)
			{
				for (double value : { -turn, turn })
				{
					if (value < state.minimum || value > state.maximum)
						continue;
					const int y = int(plot.top() + plot.height() * (state.maximum - value) / span);
					// A landmark on the frame is the frame, not a reading.
					if (y <= int(plot.top()) + 2 || y >= int(plot.bottom()) - 2)
						continue;
					painter.setPen(QPen(withAlpha(tokens.accent, 26), 3));
					painter.drawLine(int(plot.left()), y, int(plot.right()), y);
					painter.setPen(QPen(withAlpha(tokens.accent, 96), 1));
					painter.drawLine(int(plot.left()), y, int(plot.right()), y);
				}
			}
		}
	}

	// Zero: the knob's luminous anchor laid flat - accent bloom under a
	// text-ink core. Drawn only when the metric's zero is inside the fitted
	// range, which is not the same as inside the pane: a group delay keeps
	// zero in its fit and measures upward from it, so the anchor lands on
	// the frame edge and is demoted there rather than dropped.
	if (state.zeroVisible)
	{
		const int zy = int(state.zeroY);
		// A zero sitting on the pane's edge is a boundary, not a detent:
		// the bloom would smear into the border and read as chrome. Only
		// the new metrics can put it there - magnitude fits symmetrically
		// and always keeps its anchor mid-pane - so this never changes the
		// magnitude view.
		const bool anchorOnEdge = !magnitude
			&& (state.zeroY <= plot.top() + 2.0 || state.zeroY >= plot.bottom() - 2.0);
		if (!anchorOnEdge)
		{
			painter.setPen(QPen(withAlpha(tokens.accent, 52), 3));
			painter.drawLine(int(plot.left()), zy, int(plot.right()), zy);
		}
		painter.setPen(QPen(withAlpha(tokens.text, anchorOnEdge ? 120 : 200), 1));
		painter.drawLine(int(plot.left()), zy, int(plot.right()), zy);
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	const double zeroClamped = qBound(plot.top(), state.zeroY, plot.bottom());

	// Clipping warms the glass above 0 dB: a danger wash dying as it
	// lands on the anchor.
	if (state.clipping && zeroClamped > plot.top() + 1.0)
	{
		QLinearGradient warmth(0, plot.top(), 0, zeroClamped);
		warmth.setColorAt(0.0, withAlpha(tokens.danger, dark ? 34 : 26));
		warmth.setColorAt(1.0, withAlpha(tokens.danger, 0));
		painter.fillRect(QRectF(plot.left(), plot.top(), plot.width(), zeroClamped - plot.top()), warmth);
	}

	// One pass per segment. The response breaks where the metric has no
	// value, and the glass must break with it - a bridging stroke would
	// glow across a reading the config never produced.
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;

		// The under-fill is a gain idea, so each metric answers for it
		// separately.
		if (magnitude)
		{
			// It splits at zero: boost glows a step warmer than cut, both
			// dying as they land on the anchor.
			QPolygonF fill = segment;
			fill.append(QPointF(segment.last().x(), zeroClamped));
			fill.prepend(QPointF(segment.first().x(), zeroClamped));
			const double zeroRatio = qBound(0.02, (zeroClamped - plot.top()) / qMax(1.0, plot.height()), 0.98);
			QLinearGradient split(0, plot.top(), 0, plot.bottom());
			split.setColorAt(0.0, withAlpha(tokens.accent, 62));
			split.setColorAt(zeroRatio, withAlpha(tokens.accent, 8));
			split.setColorAt(1.0, withAlpha(tokens.accent, 34));
			painter.setPen(Qt::NoPen);
			painter.setBrush(split);
			painter.drawPolygon(fill);
		}
		else if (state.metric == AnalysisMetric::GroupDelayMs)
		{
			// A delay is a duration measured from no delay at all, so the
			// fill keeps its meaning here: it is how much time, and it
			// still dies as it lands on the anchor. What goes is the
			// split - warmer above, cooler below is a boost/cut idea, and
			// a delay that runs early is not a quieter kind of delay. The
			// two sides light equally. The anchor is usually the pane's
			// own floor (a group delay rarely goes negative), so the
			// stops collapse to a single falloff rather than leaving a
			// bright sliver in the last two percent of the pane.
			QPolygonF fill = segment;
			fill.append(QPointF(segment.last().x(), zeroClamped));
			fill.prepend(QPointF(segment.first().x(), zeroClamped));
			const double zeroRatio = qBound(0.0, (zeroClamped - plot.top()) / qMax(1.0, plot.height()), 1.0);
			QLinearGradient sink(0, plot.top(), 0, plot.bottom());
			sink.setColorAt(0.0, withAlpha(tokens.accent, zeroRatio <= 0.02 ? 8 : 48));
			if (zeroRatio > 0.02 && zeroRatio < 0.98)
				sink.setColorAt(zeroRatio, withAlpha(tokens.accent, 8));
			sink.setColorAt(1.0, withAlpha(tokens.accent, zeroRatio >= 0.98 ? 8 : 48));
			painter.setPen(Qt::NoPen);
			painter.setBrush(sink);
			painter.drawPolygon(fill);
		}
		// Phase gets no fill. The area between the trace and zero would
		// have to mean "how far from zero phase", and zero phase sits at
		// the very top of an all-pass's axis, so the fill swallows the
		// pane to say something no one reads a phase view for. Under this
		// metric the trace is the whole light of the window, which is
		// what the constitution says it is anyway.

		// The trace: four layered strokes, the glow lifted a breath
		// while the pointer holds the pane.
		painter.setBrush(Qt::NoBrush);
		const struct { double width; int alpha; int lift; } layers[] = {
			{ 9.0, 22, 10 },
			{ 5.5, 48, 14 },
			{ 3.0, 110, 20 },
			{ 1.6, 255, 0 }
		};
		for (const auto& layer : layers)
		{
			QPen glow(withAlpha(tokens.accent, qMin(255, layer.alpha + qRound(layer.lift * hover))), layer.width);
			glow.setCapStyle(Qt::RoundCap);
			glow.setJoinStyle(Qt::RoundJoin);
			painter.setPen(glow);
			painter.drawPolyline(segment);
		}

		// The overshoot segment ignites: the same stroke ladder re-drawn
		// in danger, clipped to the glass above the anchor.
		if (state.clipping && zeroClamped > plot.top())
		{
			QPainterStateGuard overshootState(&painter);
			painter.setClipRect(QRectF(plot.left(), plot.top(), plot.width(), zeroClamped - plot.top()),
				Qt::IntersectClip);
			const struct { double width; int alpha; } flames[] = {
				{ 6.5, 44 },
				{ 3.2, 130 },
				{ 1.6, 255 }
			};
			for (const auto& flame : flames)
			{
				QPen firePen(withAlpha(tokens.danger, flame.alpha), flame.width);
				firePen.setCapStyle(Qt::RoundCap);
				firePen.setJoinStyle(Qt::RoundJoin);
				painter.setPen(firePen);
				painter.drawPolyline(segment);
			}
		}
	}

	// The CLIP flag: a lit danger glass chip (type-badge grammar) at the
	// pane's top right - bloom stroke first, hairline and ink on top.
	if (state.clipping)
	{
		QFont chipFont(tokens.monoFontFamily);
		chipFont.setPointSizeF(8.0);
		chipFont.setWeight(QFont::DemiBold);
		chipFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		const QFontMetricsF chipMetrics(chipFont);
		const QString clipText = QStringLiteral("CLIP");
		const QRectF chip(plot.right() - chipMetrics.horizontalAdvance(clipText) - 20.0, plot.top() + 6.0,
			chipMetrics.horizontalAdvance(clipText) + 14.0, 16.0);
		painter.setPen(QPen(withAlpha(tokens.danger, 44), 3.0));
		painter.setBrush(withAlpha(tokens.danger, dark ? 38 : 26));
		painter.drawRoundedRect(chip, 8.0, 8.0);
		painter.setPen(QPen(withAlpha(tokens.danger, 150), 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(chip, 8.0, 8.0);
		painter.setFont(chipFont);
		painter.setPen(QColor(tokens.danger));
		painter.drawText(chip, Qt::AlignCenter, clipText);
	}

	// Cursor: a vertical light seam pooling at the reading point, the
	// indicator dot on the trace and a lit glass reading chip. The whole
	// group rides state.hover for its entry motion.
	if (state.cursorValid && hover > 0.01)
	{
		QPainterStateGuard cursorState(&painter);
		painter.setOpacity(painter.opacity() * hover);

		const double cx = state.cursor.x();
		const double curveY = qBound(plot.top(), state.curveYAtCursor, plot.bottom());
		// Inside a null the metric has no value, and the state says so by
		// leaving the reading text empty. There is nothing for the light
		// to land on, so the seam crosses the pane unpooled and dimmer,
		// and neither the dot nor the chip appears - a dot on a column
		// with no reading is a number the config never produced. Never
		// the case under magnitude, which floors instead of breaking.
		const bool reading = magnitude || !state.cursorText.isEmpty();
		const double poolAt = reading
			? qBound(0.05, (curveY - plot.top()) / qMax(1.0, plot.height()), 0.95)
			: 0.5;
		const auto seam = [&](int alpha) {
			QLinearGradient gradient(cx, plot.top(), cx, plot.bottom());
			gradient.setColorAt(0.0, withAlpha(tokens.accent, 0));
			gradient.setColorAt(poolAt, withAlpha(tokens.accent, alpha));
			gradient.setColorAt(1.0, withAlpha(tokens.accent, 0));
			return gradient;
		};
		// Bloom, mid, core: the insert seam's stroke ladder set upright.
		QPen seamBloom(QBrush(seam(reading ? 56 : 26)), 5.0);
		seamBloom.setCapStyle(Qt::RoundCap);
		painter.setPen(seamBloom);
		painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));
		QPen seamMid(QBrush(seam(reading ? 140 : 62)), 2.4);
		seamMid.setCapStyle(Qt::RoundCap);
		painter.setPen(seamMid);
		painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));
		QPen seamCore(QBrush(seam(reading ? 235 : 96)), 1.0);
		seamCore.setCapStyle(Qt::RoundCap);
		painter.setPen(seamCore);
		painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));

		// The reading point: the indicator dot (halo + core) on the trace.
		if (reading)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(tokens.accent, 110));
			painter.drawEllipse(QPointF(cx, curveY), 6.0, 6.0);
			painter.setBrush(QColor(tokens.accent));
			painter.drawEllipse(QPointF(cx, curveY), 3.0, 3.0);
		}

		// The reading chip: sunken glass over the pane, accent-lit edge,
		// DM Mono value ink. It follows the dot and flips or clamps to
		// stay on the glass.
		if (!state.cursorText.isEmpty())
		{
			QFont readoutFont(tokens.monoFontFamily);
			readoutFont.setPointSizeF(8.5);
			readoutFont.setWeight(QFont::DemiBold);
			const QFontMetricsF readoutMetrics(readoutFont);
			const QString readout = readoutMetrics.elidedText(state.cursorText, Qt::ElideRight,
				qMax(20.0, plot.width() - 24.0));
			const double chipWidth = readoutMetrics.horizontalAdvance(readout) + 16.0;
			const double chipHeight = 18.0;
			double chipX = cx + 10.0;
			if (chipX + chipWidth > plot.right() - 4.0)
				chipX = cx - 10.0 - chipWidth;
			chipX = qBound(plot.left() + 4.0, chipX, qMax(plot.left() + 4.0, plot.right() - chipWidth - 4.0));
			double chipY = curveY - chipHeight - 8.0;
			if (chipY < plot.top() + 4.0)
				chipY = curveY + 8.0;
			chipY = qBound(plot.top() + 4.0, chipY, qMax(plot.top() + 4.0, plot.bottom() - chipHeight - 4.0));
			const QRectF chip(chipX, chipY, chipWidth, chipHeight);

			painter.setPen(QPen(withAlpha(tokens.accent, 44), 3.0));
			painter.setBrush(withAlpha(tokens.graph, dark ? 222 : 240));
			painter.drawRoundedRect(chip, 8.0, 8.0);
			painter.setPen(QPen(withAlpha(tokens.accent, 130), 1.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(chip, 8.0, 8.0);
			painter.setFont(readoutFont);
			painter.setPen(QColor(tokens.text));
			painter.drawText(chip, Qt::AlignCenter, readout);
		}
	}

	// The pane's edge: a hairline border over a darker inner top edge.
	painter.setClipping(false);
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QColor(tokens.border), 1.0));
	painter.drawRoundedRect(frame, 8.0, 8.0);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.fillRect(QRectF(frame.left() + 7.0, frame.top() + 1.0, frame.width() - 14.0, 1.0),
		dark ? QColor(0, 0, 0, 140) : QColor(0, 0, 0, 30));
}
