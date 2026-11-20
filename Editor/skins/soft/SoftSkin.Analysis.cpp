/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SoftSkin.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "SoftPaint.h"

namespace
{
// The warning mark of the clipping notice: a stroke triangle with its
// exclamation, in the failure colour.
void paintWarnMark(QPainter& painter, const QRectF& box, const QColor& ink)
{
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	const qreal s = qMin(box.width(), box.height()) / 16.0;
	const auto at = [&](qreal x, qreal y) {
			return QPointF(box.left() + x * s, box.top() + y * s);
		};
	QPainterPath path;
	path.moveTo(at(8.0, 2.5));
	path.lineTo(at(14.0, 13.0));
	path.lineTo(at(2.0, 13.0));
	path.closeSubpath();
	painter.drawPath(path);
	painter.drawLine(at(8.0, 6.5), at(8.0, 9.5));
	painter.drawLine(at(8.0, 11.3), at(8.0, 11.4));
	painter.restore();
}
}

// The analysis dock's response graph, drawn the way a plot is drawn in any
// measuring program: a panel, a hairline grid, one line. EqGraphView owns
// the sampling, the axis fit and the cursor.
//
// The line is the emphasis colour. Where the configuration can clip
// (state.clipping), the stretch above 0 dB is the failure colour, one pixel
// heavier and over a light fill of the same colour: the heavier line and the
// fill carry the state for a reader who cannot tell the two colours apart.
void SoftSkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
	const QColor accent(tokens.accent);
	const QColor muted(tokens.mutedText);
	const QColor line(tokens.border);
	const QColor danger(tokens.danger);

	const QRectF frame(state.rect);
	QPainterPath panelPath;
	panelPath.addRoundedRect(frame.adjusted(0.5, 0.5, -0.5, -0.5), SoftPaint::surfaceRadius - 0.5,
		SoftPaint::surfaceRadius - 0.5);

	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	SoftPaint::box(painter, frame, QColor(tokens.card), line, SoftPaint::surfaceRadius);

	QPainterStateGuard panelState(&painter);
	painter.setClipPath(panelPath);

	const QFont labelFont = SoftPaint::numberFont(tokens, 12);
	const QFontMetricsF labelMetrics(labelFont);
	painter.setFont(labelFont);

	// The grid: a hairline for every labelled frequency and every value row.
	// Straight lines are drawn as filled pixel rows so they stay one device
	// pixel at any scale.
	for (const AnalysisGraphState::GridLine& gridLine : state.vertical)
	{
		if (gridLine.major || !gridLine.label.isEmpty())
			painter.fillRect(QRectF(qFloor(gridLine.pos), state.plotRect.top(), 1.0, state.plotRect.height()), line);
	}
	const bool magnitude = state.metric == AnalysisMetric::MagnitudeDb;
	for (const AnalysisGraphState::GridLine& gridLine : state.horizontal)
	{
		if (gridLine.pos < state.plotRect.top() - 0.5 || gridLine.pos > state.plotRect.bottom() + 0.5)
			continue;
		painter.fillRect(QRectF(state.plotRect.left(), qFloor(gridLine.pos), state.plotRect.width(), 1.0), line);
	}

	// The zero row, one step heavier than the grid. Drawn only while zero is
	// inside the fitted range - and, for the metrics that can push it onto a
	// frame edge (a group delay that never goes negative, a phase that only
	// descends), only while it is far enough inside to be a landmark: a line
	// lying along the floor reads as the floor.
	const bool groundIsLandmark = state.zeroVisible
		&& (magnitude || (state.zeroY > state.plotRect.top() + 6.0
			&& state.zeroY < state.plotRect.bottom() - 6.0));
	if (groundIsLandmark)
		painter.fillRect(QRectF(state.plotRect.left(), qFloor(state.zeroY), state.plotRect.width(), 1.0),
			QColor(tokens.controlEdge));

	// The response. One piece per stretch the metric has a reading for; a
	// gap in the reading is a gap in the line.
	const qreal splitY = qBound(frame.top(), qreal(state.zeroY), frame.bottom());
	const QRectF aboveZero(frame.left() - 2.0, frame.top() - 2.0, frame.width() + 4.0, splitY - frame.top() + 2.0);
	const QRectF belowZero(frame.left() - 2.0, splitY, frame.width() + 4.0, frame.bottom() - splitY + 2.0);
	painter.setBrush(Qt::NoBrush);
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;
		if (!magnitude || !state.clipping)
		{
			painter.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			continue;
		}

		{
			QPainterStateGuard pass(&painter);
			painter.setClipRect(belowZero, Qt::IntersectClip);
			painter.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
		}
		{
			QPainterStateGuard pass(&painter);
			painter.setClipRect(aboveZero, Qt::IntersectClip);
			const double base = state.clampedZeroY();
			QPolygonF area = segment;
			area.append(QPointF(segment.last().x(), base));
			area.prepend(QPointF(segment.first().x(), base));
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlphaF(danger, tokens.dark ? 0.22 : 0.16));
			painter.drawPolygon(area);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(danger, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
		}
	}

	// The frequency figures under the plot; the two at the ends tuck inside
	// the panel's rounding.
	painter.setPen(muted);
	for (int i = 0; i < state.vertical.size(); i++)
	{
		const AnalysisGraphState::GridLine& gridLine = state.vertical.at(i);
		if (gridLine.label.isEmpty())
			continue;
		QRectF labelRect = skinXTickLabelRect(gridLine.pos, state.plotRect.bottom() + 3.0, 14.0);
		int align = Qt::AlignHCenter;
		if (labelRect.right() > state.rect.right() - 8)
		{
			labelRect.setRight(state.rect.right() - 8);
			align = Qt::AlignRight;
		}
		if (labelRect.left() < state.rect.left() + 8)
		{
			labelRect.setLeft(state.rect.left() + 8);
			align = Qt::AlignLeft;
		}
		painter.drawText(labelRect, align | Qt::AlignTop, gridLine.label);
	}

	// The value figures sit just above their rows along the left edge,
	// thinned when the fitted range packs the rows tighter than a figure and
	// anchored at the zero row so the kept ones stay symmetric around it.
	// They arrive worded for whichever metric is showing.
	int groundIndex = 0;
	for (int i = 0; i < state.horizontal.size(); i++)
	{
		if (state.horizontal.at(i).major)
		{
			groundIndex = i;
			break;
		}
	}
	qreal rowGap = 0.0;
	if (state.horizontal.size() >= 2)
		rowGap = qAbs(state.horizontal.at(1).pos - state.horizontal.at(0).pos);
	const int labelStride = rowGap > 0.5 ? qMax(1, qCeil(18.0 / rowGap)) : 1;
	for (int i = 0; i < state.horizontal.size(); i++)
	{
		const AnalysisGraphState::GridLine& gridLine = state.horizontal.at(i);
		if (gridLine.label.isEmpty() || qAbs(i - groundIndex) % labelStride != 0)
			continue;
		painter.drawText(QRectF(state.plotRect.left() + 6.0, gridLine.pos - 16.0, 56.0, 14.0),
			Qt::AlignLeft | Qt::AlignVCenter, gridLine.label);
	}

	// The notice row along the top of the plot. It starts past the value
	// figures' column so it never sits on a figure.
	const qreal noticeLeft = state.plotRect.left() + 64.0;
	const qreal noticeTop = state.plotRect.top() + 5.0;
	const qreal noticeHeight = 18.0;
	const QFont nameFont = SoftPaint::bodyFont(tokens, 13, QFont::DemiBold);
	const QFont sentenceFont = SoftPaint::bodyFont(tokens, 13);
	const auto notice = [&](qreal left, const QString& name, const QString& sentence) {
			painter.setFont(nameFont);
			painter.setPen(QColor(tokens.text));
			const qreal nameWidth = QFontMetricsF(nameFont).horizontalAdvance(name);
			painter.drawText(QRectF(left, noticeTop, nameWidth + 2.0, noticeHeight), Qt::AlignLeft | Qt::AlignVCenter, name);
			// The sentence stops short of the cursor readout's corner and
			// stands down when the pane is too narrow to hold it: a clipped
			// explanation explains nothing.
			const QRectF sentenceRect(left + nameWidth + 10.0, noticeTop,
				qMax(0.0, state.plotRect.right() - 150.0 - (left + nameWidth + 10.0)), noticeHeight);
			if (sentenceRect.width() >= 120.0)
			{
				painter.setFont(sentenceFont);
				painter.setPen(muted);
				painter.drawText(sentenceRect, Qt::AlignLeft | Qt::AlignVCenter,
					QFontMetricsF(sentenceFont).elidedText(sentence, Qt::ElideRight, sentenceRect.width()));
			}
		};

	// Clipping: the mark in the failure colour, what it is, and what to do.
	// No jargon (never "clipping"); the sentence is the advice.
	if (state.clipping)
	{
		paintWarnMark(painter, QRectF(noticeLeft, noticeTop + 1.0, 16.0, 16.0), danger);
		notice(noticeLeft + 22.0, QCoreApplication::translate("SoftSkin", "Over 0 dB"),
			QCoreApplication::translate("SoftSkin", "Sound may distort - keep it below 0 dB"));
	}

	// Under phase and group delay the grid figures are bare signed numbers,
	// so the row names the quantity with the unit the state handed over and
	// says in a sentence what the view shows.
	if (!magnitude)
	{
		const bool phase = state.metric == AnalysisMetric::PhaseDegrees;
		notice(noticeLeft,
			phase ? QCoreApplication::translate("SoftSkin", "Phase in %1").arg(state.unit)
			: QCoreApplication::translate("SoftSkin", "Delay in %1").arg(state.unit),
			phase ? QCoreApplication::translate("SoftSkin", "How far each pitch is turned - the volume stays the same")
			: QCoreApplication::translate("SoftSkin", "How long each pitch is held back before you hear it"));
	}

	// The cursor: a hairline at the pointer's frequency, a dot on the
	// response, and the reading as a tag in the top-right corner. The hover
	// progress fades the group in.
	const double entry = qBound(0.0, state.hover, 1.0);
	const bool cursorShown = state.cursorValid && entry > 0.01;

	// Channel and sample rate rest in that corner while the pointer is away
	// (under the axis row they sat on the frequency figures). Localized data,
	// drawn as it arrives.
	if (!state.channelText.isEmpty() && !(cursorShown && !state.cursorText.isEmpty()))
	{
		painter.setFont(labelFont);
		painter.setPen(muted);
		const qreal width = qMin<qreal>(labelMetrics.horizontalAdvance(state.channelText) + 4.0,
			state.plotRect.width() / 2.0);
		painter.drawText(QRectF(state.plotRect.right() - width - 8.0, noticeTop, width, noticeHeight),
			Qt::AlignRight | Qt::AlignVCenter, labelMetrics.elidedText(state.channelText, Qt::ElideLeft, width));
	}
	if (cursorShown)
	{
		QPainterStateGuard cursorState(&painter);
		painter.setOpacity(entry);
		painter.fillRect(QRectF(qFloor(state.cursor.x()), state.plotRect.top(), 1.0, state.plotRect.height()),
			QColor(tokens.controlEdge));

		// A column the metric has no reading for gets the hairline but no
		// dot: the pointer's frequency is real, a dot parked on the axis edge
		// would claim a value nobody measured.
		if (magnitude || !state.cursorText.isEmpty())
		{
			const bool overshoot = magnitude && state.clipping && state.curveYAtCursor < state.zeroY - 0.5;
			painter.setPen(QPen(overshoot ? danger : accent, 2.0));
			painter.setBrush(QColor(tokens.card));
			painter.drawEllipse(QPointF(state.cursor.x(), state.curveYAtCursor), 4.5, 4.5);
		}

		if (!state.cursorText.isEmpty())
		{
			const qreal tagHeight = 22.0;
			const qreal tagWidth = qMin<qreal>(labelMetrics.horizontalAdvance(state.cursorText) + 18.0,
				state.plotRect.width() - 12.0);
			const QRectF tag(state.plotRect.right() - tagWidth - 6.0, state.plotRect.top() + 3.0, tagWidth, tagHeight);
			SoftPaint::box(painter, tag, QColor(tokens.card), QColor(tokens.borderStrong), tagHeight / 2.0);
			painter.setFont(labelFont);
			painter.setPen(QColor(tokens.text));
			painter.drawText(tag, Qt::AlignCenter,
				labelMetrics.elidedText(state.cursorText, Qt::ElideRight, tagWidth - 14.0));
		}
	}
}
