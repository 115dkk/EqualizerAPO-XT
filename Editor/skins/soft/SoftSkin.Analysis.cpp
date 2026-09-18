/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

// The analysis dock's response graph: "the friendly response line".
// EqGraphView owns the sampling, the axis fit and the cursor; every
// pixel here is this skin's. The pane is a paper panel like the control
// bar beside it, and the response is ONE LINE of accent ink on it - the
// accepted mockup's calm reading of what the config does. The terrain
// masses of the earlier round (cut valleys, boost hills, the warning
// pastel) retired with the concept swap: a line says the same thing
// with a tenth of the ink.
void SoftSkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
	const QColor accent(tokens.accent);
	const QColor muted(tokens.mutedText);
	const QColor border(tokens.border);
	const QColor card(tokens.card);
	const QColor warning(tokens.warning);
	const QColor tint(tokens.cardSelected);

	// The panel: the paper face on its 2px darker base step, closed by the
	// light 1px border. The step is painted here, not styled - the painter
	// owns every pixel of this widget, so a sheet border would be painted
	// over.
	QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -2.5);
	const qreal round = 18.0;
	QPainterPath facePath;
	facePath.addRoundedRect(frame, round, round);

	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(mixColor(QColor(tokens.background), border, 0.5));
	painter.drawRoundedRect(frame.translated(0.0, 2.0), round, round);
	painter.setBrush(card);
	painter.drawPath(facePath);

	QPainterStateGuard faceState(&painter);
	painter.setClipPath(facePath);

	// Axis captions ride the body face in faded ink, exactly like the
	// GraphicEQ plot (the constitution reserves mono for source text).
	QFont labelFont(tokens.fontFamily);
	labelFont.setPointSizeF(8.5);
	labelFont.setWeight(QFont::DemiBold);
	painter.setFont(labelFont);
	const QColor labelInk = withAlpha(muted, 210);

	// The grid: the frequency decades and the value rows the state hands
	// over, both as the border sunk most of the way into the paper (the
	// mockup keeps its rows), crisp with antialiasing off. The zero row
	// is the soft notch below, so its grid twin is skipped.
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QPen(mixColor(border, card, 0.25), 1));
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		if (line.major)
			painter.drawLine(qRound(line.pos), int(state.plotRect.top()), qRound(line.pos), int(state.plotRect.bottom()));
	}
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		if (line.major && qAbs(line.pos - state.zeroY) > 1.0)
			painter.drawLine(int(state.plotRect.left()), qRound(line.pos), int(state.plotRect.right()), qRound(line.pos));
	}
	painter.setRenderHint(QPainter::Antialiasing, true);

	// The response: one accent line, round caps and joins, no mass under
	// it. Phase keeps a pale ribbon under the line (the knob track's
	// pastel: a phase is a path, not a height) and group delay keeps its
	// pale stems from the ground (a wait measured from none), so the two
	// still tell apart from a level at a glance; magnitude is the bare
	// line. One line per piece of the response: where the metric has no
	// reading the line simply stops, because a line carried across that
	// gap would claim values nobody measured.
	const bool magnitude = state.metric == AnalysisMetric::MagnitudeDb;
	const QColor bodyPastel = mixColor(accent, card, 0.78);
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;

		if (state.metric == AnalysisMetric::PhaseDegrees)
		{
			painter.setPen(QPen(bodyPastel, 9.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(segment);
		}
		else if (!magnitude)
		{
			// The stems are spaced off the pane's own left edge, not off
			// each piece, so the comb stays in step across a break.
			const double ground = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
			painter.setPen(QPen(bodyPastel, 5.0, Qt::SolidLine, Qt::RoundCap));
			for (const QPointF& point : segment)
			{
				if (qRound(point.x() - state.plotRect.left()) % 26 != 0)
					continue;
				painter.drawLine(QPointF(point.x(), ground), point);
			}
		}

		painter.setPen(QPen(accent, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(segment);
	}

	// The calm ground line: the soft zero notch, rounded ends floating
	// clear of the panel walls. Drawn only while zero is really inside the
	// fitted range - and, for the metrics that can push it onto a frame
	// edge (a group delay that never goes negative, a phase that only
	// descends), only while it is far enough inside to be a landmark. A
	// notch lying along the floor is read as the floor. Magnitude fits
	// symmetrically, so its ground is always the middle of the pane and
	// this clearance never applies to it.
	const bool groundIsLandmark = state.zeroVisible
		&& (magnitude || (state.zeroY > state.plotRect.top() + 6.0
			&& state.zeroY < state.plotRect.bottom() - 6.0));
	if (groundIsLandmark)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.text), 110), 2, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(state.plotRect.left() + 6.0, state.zeroY),
			QPointF(state.plotRect.right() - 6.0, state.zeroY));
	}

	// The frequency axis speaks: the decade figures plus the 20/20k
	// endpoints anchoring the range; the in-between ticks stay
	// whitespace. Edge captions tuck inside the rounding.
	painter.setPen(labelInk);
	for (int i = 0; i < state.vertical.size(); i++)
	{
		const AnalysisGraphState::GridLine& line = state.vertical.at(i);
		if (line.label.isEmpty() || (!line.major && i != 0 && i != state.vertical.size() - 1))
			continue;
		QRect labelRect(qRound(line.pos) - 24, int(state.plotRect.bottom()) + 2, 48, 12);
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
		painter.drawText(labelRect, align | Qt::AlignTop, line.label);
	}

	// The value figures rest just above their rows along the left edge,
	// thinned to a calm cadence when the fitted range packs the rows
	// tighter than a caption, anchored at the zero ground so the kept
	// figures stay symmetric around it. They arrive already worded for
	// whichever metric is showing, so nothing here spells a unit.
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
	const int labelStride = rowGap > 0.5 ? qMax(1, qCeil(16.0 / rowGap)) : 1;
	for (int i = 0; i < state.horizontal.size(); i++)
	{
		const AnalysisGraphState::GridLine& line = state.horizontal.at(i);
		if (line.label.isEmpty() || qAbs(i - groundIndex) % labelStride != 0)
			continue;
		painter.drawText(QRectF(state.plotRect.left() + 6.0, line.pos - 15.0, 48.0, 12.0),
			Qt::AlignLeft | Qt::AlignVCenter, line.label);
	}

	// The footer caption stays a caption: channel and sample rate in the
	// same friendly ink, centred under the axis row. Localized data,
	// drawn as-is.
	if (!state.channelText.isEmpty())
	{
		const QFontMetrics footerMetrics(labelFont);
		painter.drawText(QRectF(state.plotRect.left(), frame.bottom() - 14.0, state.plotRect.width(), 13.0),
			Qt::AlignHCenter | Qt::AlignVCenter,
			footerMetrics.elidedText(state.channelText, Qt::ElideRight, int(state.plotRect.width())));
	}

	// The clipping notice: an outlined amber pill names it and - because
	// Soft's audience may not know that exceeding 0 dB audibly damages the
	// sound - a plain-language sentence in the same ink follows. No
	// jargon (never "clipping"), localized, attention rather than alarm.
	if (state.clipping)
	{
		const QString clipText = QStringLiteral("Over 0 dB");
		const QFontMetrics chipMetrics(labelFont);
		const qreal chipH = 18.0;
		const qreal chipW = chipMetrics.horizontalAdvance(clipText) + 16.0;
		const QRectF chip(state.plotRect.left() + 8.0, state.plotRect.top() + 6.0, chipW, chipH);
		painter.setPen(QPen(warning, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(chip, chipH / 2.0, chipH / 2.0);
		painter.setPen(warning);
		painter.drawText(chip, Qt::AlignCenter, clipText);

		const QString advice = QCoreApplication::translate("SoftSkin",
			"Sound may distort - keep it below 0 dB");
		QFont adviceFont(labelFont);
		adviceFont.setWeight(QFont::DemiBold);
		const QFontMetrics adviceMetrics(adviceFont);
		const QRectF adviceRect(chip.right() + 8.0, chip.top(),
			qMax(0.0, state.plotRect.right() - chip.right() - 16.0), chipH);
		if (adviceRect.width() >= 60.0)
		{
			painter.setFont(adviceFont);
			painter.setPen(withAlpha(warning, 200));
			painter.drawText(adviceRect, Qt::AlignLeft | Qt::AlignVCenter,
				adviceMetrics.elidedText(advice, Qt::ElideRight, int(adviceRect.width())));
			painter.setFont(labelFont);
		}
	}

	// Naming what is on the pane. Everybody knows a dB, and the grid
	// figures are bare signed numbers in every metric, so under phase and
	// group delay nothing here would say what those numbers count. This is
	// the skin that names things: a quiet chip carries the quantity with
	// the unit the state handed over (never a unit spelled here), and the
	// plain-language line beside it says what the view means. The row
	// starts past the value-figure column so it can never sit on a
	// figure, however tightly the fitted range packs the rows.
	if (!magnitude)
	{
		const bool phase = state.metric == AnalysisMetric::PhaseDegrees;
		const QString name = phase
			? QCoreApplication::translate("SoftSkin", "Phase in %1").arg(state.unit)
			: QCoreApplication::translate("SoftSkin", "Delay in %1").arg(state.unit);
		const QString meaning = phase
			? QCoreApplication::translate("SoftSkin", "How far each pitch is turned - the volume stays the same")
			: QCoreApplication::translate("SoftSkin", "How long each pitch is held back before you hear it");

		const QFontMetrics nameMetrics(labelFont);
		const qreal chipH = 18.0;
		const qreal chipW = nameMetrics.horizontalAdvance(name) + 16.0;
		const QRectF chip(state.plotRect.left() + 58.0, state.plotRect.top() + 6.0, chipW, chipH);
		painter.setPen(QPen(border, 1));
		painter.setBrush(QColor(tokens.surfaceSunken));
		painter.drawRoundedRect(chip, chipH / 2.0, chipH / 2.0);
		painter.setPen(QColor(tokens.text));
		painter.drawText(chip, Qt::AlignCenter, name);

		// The sentence stops well short of the readout pill's corner, and
		// stands down entirely when the pane is too narrow to hold it -
		// a clipped explanation explains nothing.
		const QRectF meaningRect(chip.right() + 10.0, chip.top(),
			qMax(0.0, state.plotRect.right() - 150.0 - chip.right() - 10.0), chipH);
		if (meaningRect.width() >= 120.0)
		{
			painter.setPen(labelInk);
			painter.drawText(meaningRect, Qt::AlignLeft | Qt::AlignVCenter,
				nameMetrics.elidedText(meaning, Qt::ElideRight, int(meaningRect.width())));
		}
	}

	// The cursor: a soft vertical notch guide (the detent grammar stood
	// upright), a rounded lens dot sitting on the line in the accent, and
	// the readout as a chosen pill (tint, accent ink, the mixed edge) in
	// the pane's top-right corner. The hover progress floats the whole
	// group in, the pill drifting down to its resting spot.
	const double entry = qBound(0.0, state.hover, 1.0);
	if (state.cursorValid && entry > 0.01)
	{
		QPainterStateGuard cursorState(&painter);
		painter.setOpacity(entry);

		painter.setPen(QPen(withAlpha(QColor(tokens.text), 70), 2, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(state.cursor.x(), state.plotRect.top() + 6.0),
			QPointF(state.cursor.x(), state.plotRect.bottom() - 6.0));

		// The lens: a paper disc with the accent ring under a quiet
		// text-ink halo, so it reads on the line and on the bare paper
		// alike. A column the metric has no reading for gets the guide but
		// no lens: the pointer's frequency is real, while a dot parked on
		// the axis edge would claim a value nobody measured.
		if (magnitude || !state.cursorText.isEmpty())
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.text), 70), 3));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(QPointF(state.cursor.x(), state.curveYAtCursor), 7.5, 7.5);
			painter.setPen(QPen(accent, 2));
			painter.setBrush(card);
			painter.drawEllipse(QPointF(state.cursor.x(), state.curveYAtCursor), 5.0, 5.0);
		}

		if (!state.cursorText.isEmpty())
		{
			const QFontMetrics pillMetrics(labelFont);
			const qreal pillH = 18.0;
			const qreal pillW = qMin<qreal>(pillMetrics.horizontalAdvance(state.cursorText) + 16.0,
					state.plotRect.width() - 12.0);
			const QRectF pill(state.plotRect.right() - pillW - 6.0,
				state.plotRect.top() + 6.0 - (1.0 - entry) * 8.0, pillW, pillH);
			painter.setPen(QPen(mixColor(card, accent, 0.42), 1));
			painter.setBrush(tint);
			painter.drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
			painter.setPen(accent);
			painter.drawText(pill, Qt::AlignCenter,
				pillMetrics.elidedText(state.cursorText, Qt::ElideRight, int(pillW - 12.0)));
		}
	}

	faceState.restore();

	// The panel edge: the very light 1px line of the two-step elevation.
	painter.setPen(QPen(border, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(facePath);
}
