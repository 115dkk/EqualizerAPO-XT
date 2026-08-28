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

// The analysis dock's response graph: "the friendly response landscape".
// EqGraphView owns the sampling, the axis fit and the cursor; every
// pixel here is the GraphicEQ instrument's family answer, adapted to a
// wide always-on monitoring readout. The response draws as TERRAIN:
// opaque pastel masses in the ON-fill grammar - cut valleys in accent,
// boost hills in success, warming to the warning pastel the moment the
// config can clip (state.clipping), named by an "Over 0 dB" chip.
void SoftSkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
	const QColor accent(tokens.accent);
	const QColor muted(tokens.mutedText);
	const QColor border(tokens.border);
	const QColor well(tokens.surfaceSunken);
	const QColor warmInk(QStringLiteral("#2B251D"));

	QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal wellRound = 14.0;
	QPainterPath wellPath;
	wellPath.addRoundedRect(frame, wellRound, wellRound);

	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::TextAntialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(well);
	painter.drawPath(wellPath);

	QPainterStateGuard wellState(&painter);
	painter.setClipPath(wellPath);

	// Axis captions ride the body face in faded ink, exactly like the
	// GraphicEQ plot (the constitution reserves mono for value chips).
	QFont labelFont(tokens.fontFamily);
	labelFont.setPointSizeF(8.5);
	labelFont.setWeight(QFont::DemiBold);
	painter.setFont(labelFont);
	const QColor labelInk = withAlpha(muted, 210);

	// Major-only grid, the border sunk most of the way into the well;
	// straight lines stay crisp with antialiasing off. The horizontal
	// majors' only member is the zero row, which the soft notch draws
	// itself, so only the frequency decades remain - whitespace does the
	// rest (tiebreaker).
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QPen(mixColor(border, well, 0.25), 1));
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		if (line.major)
			painter.drawLine(qRound(line.pos), int(state.plotRect.top()), qRound(line.pos), int(state.plotRect.bottom()));
	}
	painter.setRenderHint(QPainter::Antialiasing, true);

	// The response terrain: opaque pastel masses split at the ground line
	// by clip rects, so the semantic colour change lands exactly on the
	// zero crossing (the GraphicEQ instrument's seam trick). Each pass
	// lays the mass, then its warm-ink stroke on the terrain edge. One
	// landscape per piece of the response: where the metric has no reading
	// the ground simply ends, because a mass carried across that gap would
	// show a hill nobody measured.
	//
	// Terrain is a landscape of LEVEL, so only magnitude gets it. The two
	// metrics that are not levels answer in their own form below, sharing
	// this instrument's vocabulary - the knob track's pale body pastel,
	// the warm-ink edge stroke, rounded ends everywhere - but not its
	// masses, and never its danger side: rising above the ground is only
	// dangerous when the quantity is a gain.
	const bool magnitude = state.metric == AnalysisMetric::MagnitudeDb;
	const QColor bodyPastel = mixColor(accent, well, 0.68);
	const QColor traceInk = mixColor(accent, warmInk, 0.40);
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;

		if (!magnitude)
		{
			if (state.metric == AnalysisMetric::PhaseDegrees)
			{
				// A turn is not a height above anything, so there is no
				// mass to lay. A fill to zero would say "how far from
				// unity gain" about a quantity that has no gain, and since
				// an all-pass's phase only descends, zero sits on the top
				// wall and that fill swallows the whole pane. What a phase
				// is, is a path - so it gets the knob's track law instead:
				// the always-visible pale pastel ribbon with the deeper
				// value-arc ink riding it. The rounded ends are what make
				// a piece honest; where the phase has no reading the
				// ribbon simply stops instead of being carried across.
				painter.setPen(QPen(bodyPastel, 9.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				painter.drawPolyline(segment);
			}
			else
			{
				// A group delay is a WAIT, measured from no delay at all,
				// and this skin already draws a level standing on a line:
				// the GraphicEQ's rounded stems. One stem every roomy
				// step, grown from the ground to the reading, so that a
				// plain Delay - every pitch held back by the same amount -
				// reads as a calm even comb instead of the solid slab a
				// filled mass would make of it. Whitespace between the
				// stems does the rest (tiebreaker). The stems are spaced
				// off the pane's own left edge, not off each piece, so the
				// comb stays in step across a break.
				const double ground = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
				painter.setPen(QPen(bodyPastel, 5.0, Qt::SolidLine, Qt::RoundCap));
				for (const QPointF& point : segment)
				{
					if (qRound(point.x() - state.plotRect.left()) % 26 != 0)
						continue;
					painter.drawLine(QPointF(point.x(), ground), point);
				}
			}
			painter.setPen(QPen(traceInk, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(segment);
			continue;
		}

		const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
		QPolygonF terrain = segment;
		terrain.append(QPointF(segment.last().x(), base));
		terrain.prepend(QPointF(segment.first().x(), base));

		const QColor overFill(state.clipping ? tokens.warning : tokens.success);
		const qreal splitY = qBound(frame.top(), qreal(state.zeroY), frame.bottom());
		const QRectF aboveZero(frame.left() - 2.0, frame.top() - 2.0, frame.width() + 4.0, splitY - frame.top() + 2.0);
		const QRectF belowZero(frame.left() - 2.0, splitY, frame.width() + 4.0, frame.bottom() - splitY + 2.0);
		for (int pass = 0; pass < 2; pass++)
		{
			const bool overshootPass = pass == 0;
			const QColor side = overshootPass ? overFill : accent;
			QPainterStateGuard curvePassState(&painter);
			painter.setClipRect(overshootPass ? aboveZero : belowZero, Qt::IntersectClip);
			painter.setPen(Qt::NoPen);
			painter.setBrush(side);
			painter.drawPolygon(terrain);
			painter.setPen(QPen(mixColor(side, warmInk, 0.40), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			painter.drawPolyline(segment);
		}
	}

	// The calm ground line: the soft zero notch, rounded ends floating
	// clear of the well walls, laid over the masses so the ground level
	// reads through the hills. Drawn only while zero is really inside the
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

	// The value figures rest just above their (unpainted) rows along the
	// left edge, thinned to a calm cadence when the fitted range packs
	// the rows tighter than a caption, anchored at the zero ground so
	// the kept figures stay symmetric around it. They arrive already
	// worded for whichever metric is showing, so nothing here spells a
	// unit.
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
		painter.drawText(QRectF(state.plotRect.left(), state.rect.bottom() - 14.0, state.plotRect.width(), 13.0),
			Qt::AlignHCenter | Qt::AlignVCenter,
			footerMetrics.elidedText(state.channelText, Qt::ElideRight, int(state.plotRect.width())));
	}

	// The clipping notice: the overshoot terrain has already warmed to
	// the warning pastel; a stadium chip names it and - because Soft's
	// audience may not know that exceeding 0 dB audibly damages the
	// sound - a plain-language warning sentence follows the chip. No
	// jargon (never "clipping"), localized, and bold enough to matter.
	if (state.clipping)
	{
		const QString clipText = QStringLiteral("Over 0 dB");
		const QFontMetrics chipMetrics(labelFont);
		const qreal chipH = 18.0;
		const qreal chipW = chipMetrics.horizontalAdvance(clipText) + 16.0;
		const QRectF chip(state.plotRect.left() + 8.0, state.plotRect.top() + 6.0, chipW, chipH);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(tokens.warning));
		painter.drawRoundedRect(chip, chipH / 2.0, chipH / 2.0);
		painter.setPen(warmInk);
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
			painter.setPen(mixColor(QColor(tokens.warning), warmInk, 0.55));
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
	// plain-language line beside it says what the view means - the same
	// voice as the clip advice, and the answer to why the magnitude view
	// of these filters looked like nothing was happening. The row starts
	// past the value-figure column so it can never sit on a figure,
	// however tightly the fitted range packs the rows.
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
		painter.setBrush(QColor(tokens.card));
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
	// upright), a rounded lens dot sitting on the response in its side's
	// pastel, and the readout as an ON-pastel stadium pill in the well's
	// top-right corner. The hover progress floats the whole group in,
	// the pill drifting down to its resting spot.
	const double entry = qBound(0.0, state.hover, 1.0);
	if (state.cursorValid && entry > 0.01)
	{
		QPainterStateGuard cursorState(&painter);
		painter.setOpacity(entry);

		painter.setPen(QPen(withAlpha(QColor(tokens.text), 70), 2, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(state.cursor.x(), state.plotRect.top() + 6.0),
			QPointF(state.cursor.x(), state.plotRect.bottom() - 6.0));

		// The lens: an elevated card face wearing its side's warm-ink
		// ring under a quiet text-ink halo, so it reads both on a
		// terrain mass of the same pastel and on the bare well. Above the
		// ground means "louder than unity" only where the quantity is a
		// gain, so the level colours are magnitude's alone; a phase or a
		// wait has no dangerous side and keeps the calm accent all the way
		// across. And a column the metric has no reading for gets the
		// guide but no lens: the pointer's frequency is real, while a dot
		// parked on the axis edge would claim a value nobody measured.
		const bool overshoot = magnitude && state.curveYAtCursor < state.zeroY - 0.5;
		const QColor lensSide = overshoot ? QColor(state.clipping ? tokens.warning : tokens.success) : accent;
		if (magnitude || !state.cursorText.isEmpty())
		{
			painter.setPen(QPen(withAlpha(QColor(tokens.text), 70), 3));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(QPointF(state.cursor.x(), state.curveYAtCursor), 7.5, 7.5);
			painter.setPen(QPen(mixColor(lensSide, warmInk, 0.40), 2));
			painter.setBrush(QColor(tokens.card));
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
			painter.setPen(Qt::NoPen);
			painter.setBrush(accent);
			painter.drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
			painter.setPen(warmInk);
			painter.drawText(pill, Qt::AlignCenter,
				pillMetrics.elidedText(state.cursorText, Qt::ElideRight, int(pillW - 12.0)));
		}
	}

	wellState.restore();

	// The well edge: the very light 1px line of the two-step elevation.
	painter.setPen(QPen(border, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(wellPath);
}
