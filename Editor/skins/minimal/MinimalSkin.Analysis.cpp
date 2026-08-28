/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QStringList>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// What the sheet is a record OF. A plotter sheet that does not say what it
// plotted is not a record, and the two new metrics have no danger tag and no
// dB in their figures to give it away. Magnitude keeps the RESPONSE masthead
// it has printed since this graph existed.
QString minimalSheetHeading(AnalysisMetric metric)
{
	switch (metric)
	{
	case AnalysisMetric::PhaseDegrees:
		return QStringLiteral("PHASE");
	case AnalysisMetric::GroupDelayMs:
		return QStringLiteral("GROUP DELAY");
	case AnalysisMetric::MagnitudeDb:
		break;
	}
	return QStringLiteral("RESPONSE");
}
}

// The analysis dock's response graph as this skin's plotter sheet: the
// measurement-record grammar of the GraphicEQ plot stretched into a wide
// always-on lab chart. Straight lines land on half-pixel centres so they
// stay crisp with antialiasing on. The sheet prints its own record header
// top-left, so an empty config's flat trace still reads as a deliberate
// record; the footer channel/sample-rate caption is sheet metadata, printed
// as-is (localized data) and elided, never overflowed.
void MinimalSkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
	const QColor ground(tokens.graph);
	const QColor gridMinor(tokens.graphGridMinor);
	const QColor gridMajor(tokens.graphGridMajor);
	const QColor secondary(tokens.mutedText);
	const QColor bodyInk(tokens.text);
	// Every dB-only idea below is guarded on this. The magnitude sheet has to
	// print exactly as it did before phase and group delay existed, so a
	// branch that is not gated here is a regression, not an improvement.
	const bool magnitudeSheet = state.metric == AnalysisMetric::MagnitudeDb;

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	painter.fillRect(state.rect, ground);

	const double plotLeft = state.plotRect.left();
	const double plotRight = state.plotRect.right();
	const double plotTop = state.plotRect.top();
	const double plotBottom = state.plotRect.bottom();

	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(8.5);

	// Vertical grid and the frequency figures under the plot. A figure that
	// would run into the previous print is skipped (majors always print) -
	// the sheet stays legible at any dock width.
	painter.setFont(labelFont);
	const QFontMetricsF labelMetrics(labelFont);
	double lastFigureRight = -1.0e9;
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		const double x = qFloor(line.pos) + 0.5;
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(QPointF(x, plotTop), QPointF(x, plotBottom));
		if (line.label.isEmpty())
			continue;
		const double halfWidth = labelMetrics.horizontalAdvance(line.label) / 2.0;
		if (!line.major && x - halfWidth < lastFigureRight + 4.0)
			continue;
		painter.setPen(secondary);
		painter.drawText(skinXTickLabelRect(x, plotBottom + 3.0, 11.0),
			Qt::AlignHCenter | Qt::AlignTop, line.label);
		lastFigureRight = x + halfWidth;
	}

	// The value figures are printed exactly as the axis prepared them,
	// whichever metric is on the sheet. On a dB sheet they live in the side
	// margins, which are narrow, so the axis font follows the knob precedent:
	// shrink to fit, never clip. The other two metrics print theirs inboard
	// (below) where the margin is not the constraint, and keep the sheet's one
	// type size.
	QFont axisFont(labelFont);
	if (magnitudeSheet)
	{
		const double marginWidth = plotLeft - state.rect.left();
		double widest = 0.0;
		for (const AnalysisGraphState::GridLine& line : state.horizontal)
			widest = qMax(widest, QFontMetricsF(axisFont).horizontalAdvance(line.label));
		while (widest > marginWidth - 2.0 && axisFont.pointSizeF() > 6.0)
		{
			axisFont.setPointSizeF(axisFont.pointSizeF() - 0.5);
			widest = 0.0;
			for (const AnalysisGraphState::GridLine& line : state.horizontal)
				widest = qMax(widest, QFontMetricsF(axisFont).horizontalAdvance(line.label));
		}
	}
	painter.setFont(axisFont);
	double lastFigureY = 1.0e9;
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		const double y = qFloor(line.pos) + 0.5;
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(QPointF(plotLeft, y), QPointF(plotRight, y));
		if (line.label.isEmpty())
			continue;
		if (!line.major && lastFigureY - y < 11.0)
			continue;
		painter.setPen(line.major ? bodyInk : secondary);
		if (magnitudeSheet)
		{
			painter.drawText(skinYTickLabelRect(y, state.rect.left(), plotLeft - state.rect.left() - 2.0),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
			painter.drawText(skinYTickLabelRect(y, plotRight + 2.0, state.rect.right() - plotRight - 2.0),
				Qt::AlignLeft | Qt::AlignVCenter, line.label);
		}
		else
		{
			// A dB figure is three characters and the margins were cut to hold
			// exactly that. "-360" and "+2.00" do not fit them at any legible
			// size, and a figure that overflows its right-aligned box loses its
			// sign first: the sheet would print a wrong number, which is worse
			// than printing an ugly one. So on these sheets the scale moves
			// inboard and each figure sits ON its own rule (the picker's
			// caption-on-a-rule law), still at both edges because a 940px sheet
			// is read from whichever side is nearer. A rule against the pane's
			// top has no room above it and prints below instead. The trace is
			// drawn after this and crosses the figures the way a plotter's pen
			// crosses the scale already printed on the paper.
			const double advance = QFontMetricsF(axisFont).horizontalAdvance(line.label);
			const double baseline = y - plotTop < 12.0 ? y + 10.0 : y - 3.0;
			painter.drawText(QPointF(plotLeft + 4.0, baseline), line.label);
			painter.drawText(QPointF(plotRight - 4.0 - advance, baseline), line.label);
		}
		lastFigureY = y;
	}

	// The clipping flag: terminal error semantics. The region between the
	// trace and the 0 dB rule fills SOLID - reverse video, the way a
	// terminal marks a line that is wrong - and the trace prints through it
	// in the sheet's ground colour (the inverted glyph). The block's ink is
	// danger SUNK into the sheet's register (hue kept, saturation and value
	// derived down): a raw semantic red at area strength hurt the eyes in
	// both finishes - a terminal's error field is dim red, not neon.
	QPainterPath overshoot;
	const bool overshootValid = state.clipping && state.zeroY > plotTop + 1.0;
	const bool darkSheet = ground.lightness() < 128;
	if (overshootValid)
	{
		// One closed block per printed piece of the trace. Clipping is a
		// magnitude reading and magnitude prints in one piece, so in practice
		// this records once; a record with gaps still gets a block per piece
		// instead of one field spanning what was never measured.
		for (const QPolygonF& segment : state.curves)
		{
			if (segment.size() < 2)
				continue;
			QPolygonF closed = segment;
			closed.append(QPointF(segment.last().x(), state.zeroY));
			closed.append(QPointF(segment.first().x(), state.zeroY));
			overshoot.addPolygon(closed);
			overshoot.closeSubpath();
		}

		// Dark sheet: a dim red field (phosphor's error register). Light
		// sheet: a black-red block - on an ink-on-paper terminal the error
		// field is HEAVY ink, so the block goes near-ink dark with the red
		// hue kept, and the paper-coloured trace inverts through it white
		// against black.
		const QColor dangerBase(tokens.danger);
		const QColor errorBlock = QColor::fromHsvF(
			dangerBase.hsvHueF(),
			dangerBase.hsvSaturationF() * (darkSheet ? 0.70 : 0.84),
			dangerBase.valueF() * (darkSheet ? 0.56 : 0.55));

		QPainterStateGuard overshootState(&painter);
		painter.setClipRect(QRectF(plotLeft, plotTop, plotRight - plotLeft, state.zeroY - plotTop));
		painter.fillPath(overshoot, errorBlock);
	}

	// The zero rule: the one full-strength straight line, body ink 1px. It
	// prints only when the metric's zero lands inside the sheet. On the two new
	// metrics that is often the sheet's own edge - a group delay keeps zero in
	// its fit and measures upward from it, a descending phase starts at it - and
	// the rule is printed there all the same, because a baseline drawn along the
	// bottom or the top of a plotter sheet is still the axis the pen was zeroed
	// against.
	if (state.zeroVisible)
	{
		const double zeroY = qFloor(state.zeroY) + 0.5;
		painter.setPen(QPen(bodyInk, 1));
		painter.drawLine(QPointF(plotLeft, zeroY), QPointF(plotRight, zeroY));
	}

	// The response: a single 1px body-ink trace. No fill, no echo - the
	// trace is data and the brightest line on the sheet. Inside the error
	// block it inverts to ground ink (reverse video keeps the glyph). One
	// pass per piece: where the metric has no reading the pen lifts off the
	// sheet, and a stroke across the gap would print a measurement that was
	// never taken.
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;
		painter.setPen(QPen(bodyInk, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(segment);
		if (overshootValid)
		{
			QPainterStateGuard overshootSegmentState(&painter);
			painter.setClipRect(QRectF(plotLeft, plotTop, plotRight - plotLeft, state.zeroY - plotTop));
			painter.setClipPath(overshoot, Qt::IntersectClip);
			painter.setPen(QPen(ground, 1));
			painter.drawPolyline(segment);
		}
	}

	// The plotter crosshair: a full-height vertical hairline with a short
	// horizontal tick at the reading. Its ink rises from the secondary
	// half-tone to body ink with the hover progress (entry motion).
	const QColor crosshairInk = mixColor(secondary, bodyInk, qBound(0.0, state.hover, 1.0));
	if (state.cursorValid)
	{
		const double cursorX = qFloor(state.cursor.x()) + 0.5;
		const double readingY = qFloor(state.curveYAtCursor) + 0.5;
		painter.setPen(QPen(crosshairInk, 1));
		painter.drawLine(QPointF(cursorX, plotTop), QPointF(cursorX, plotBottom));
		// The short tick IS the reading, so it only prints where there is one.
		// Inside a null the metric has no value and curveYAtCursor falls back to
		// the axis floor; a tick parked there would print a measurement that was
		// never taken. The prepared readout is the state's own answer to whether
		// the column has a value, and a magnitude column always has one.
		if (magnitudeSheet || !state.cursorText.isEmpty())
			painter.drawLine(QPointF(cursorX - 6.0, readingY), QPointF(cursorX + 6.0, readingY));
	}

	// Top-margin annotations: the engraved sheet header (plus the clip tag
	// when the sheet is flagged) and the cursor reading, printed like a
	// plotter's margin note in the crosshair's rising ink.
	const QRectF topBand(plotLeft, state.rect.top() + 2.0, plotRight - plotLeft, 12.0);
	QFont captionFont(tokens.monoFontFamily);
	captionFont.setPointSizeF(8.5);
	captionFont.setBold(true);
	captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	painter.setFont(captionFont);
	painter.setPen(secondary);
	const QString heading = minimalSheetHeading(state.metric);
	painter.drawText(topBand, Qt::AlignLeft | Qt::AlignVCenter, heading);
	if (state.clipping)
	{
		// Error register, not annotation: the tag prints in danger ink like
		// the reverse-video block it labels.
		const double headingWidth = QFontMetricsF(captionFont).horizontalAdvance(heading);
		painter.setPen(QColor(tokens.danger));
		painter.drawText(topBand.adjusted(headingWidth + 12.0, 0.0, 0.0, 0.0),
			Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("!! OVER 0 DB"));
	}
	else if (!magnitudeSheet && !state.spanValueText.isEmpty())
	{
		// Figures first: the scale stays bare signed numbers in every metric,
		// so the unit is named once, here, on the line that identifies the
		// record. On a dB sheet the unit rode in the OVER tag and the cursor
		// readout; phase and group delay have no tag, and a column of signed
		// numbers that never names its unit is not a reading. The span arrives
		// already formatted - a degree sign is never spelled in this file - and
		// takes the slot the error tag holds on a magnitude sheet: one masthead,
		// one register. It prints in the data register (plain, as-is) rather
		// than the caption's tracked bold, because it carries values.
		const double headingWidth = QFontMetricsF(captionFont).horizontalAdvance(heading);
		painter.setFont(labelFont);
		painter.drawText(topBand.adjusted(headingWidth + 12.0, 0.0, 0.0, 0.0),
			Qt::AlignLeft | Qt::AlignVCenter, state.spanValueText);
	}
	if (state.cursorValid && !state.cursorText.isEmpty())
	{
		painter.setFont(labelFont);
		painter.setPen(crosshairInk);
		painter.drawText(topBand, Qt::AlignRight | Qt::AlignVCenter, state.cursorText);
	}
	else if (!magnitudeSheet && state.cursorValid)
	{
		// A terminal reports an empty result instead of dressing it up (the
		// picker's NO MATCH law). The crosshair still names the frequency; the
		// readout says the pen was off the sheet in that column.
		painter.setFont(labelFont);
		painter.setPen(secondary);
		painter.drawText(topBand, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("NO READING"));
	}

	// Sheet metadata on the bottom edge: the channel/sample-rate caption,
	// localized data printed as-is in secondary ink, elided to the sheet.
	if (!state.channelText.isEmpty())
	{
		painter.setFont(labelFont);
		painter.setPen(secondary);
		const QRectF footer(plotLeft, state.rect.bottom() - 14.0, plotRight - plotLeft, 12.0);
		painter.drawText(footer, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(labelFont).elidedText(state.channelText, Qt::ElideRight, int(footer.width())));
	}

	// The frame: one square 1px hairline, the same frame the GraphicEQ
	// plot wears (half-pixel so it stays crisp under antialiasing).
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5));
}
