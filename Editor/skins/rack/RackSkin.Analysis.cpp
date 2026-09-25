/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RackSkin.h"
#include "Editor/skins/rack/RackPalette.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QRadialGradient>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "RackSkinDetail.h"

void RackSkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	const double hover = qBound(0.0, state.hover, 1.0);
	// The unit reads one of three quantities. Magnitude is the function this
	// monitor was built around, and every idiom below that assumes a gain - the
	// OVER zone, the wash to the unity rail, the dB step ladder on the figures -
	// is fenced behind this flag. Nothing outside those fences changes with the
	// function, so the magnitude face is the one it always had.
	const bool magnitude = state.metric == AnalysisMetric::MagnitudeDb;
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// Display-glass idiom shared with the GEQ scope and the LCD wells: the
	// glass is dark in both finishes, the graticule lives in the scope-grid
	// family (the cream table's grid token is panel paint, so it never
	// reaches the glass), the phosphor is the machine's LED green lifted to
	// emission strength on the cream finish, and the OVER voice is the
	// danger red lifted the same way.
	const QColor glassTop = RackPalette::GlassTop(dark);
	const QColor glassBottom = RackPalette::GlassBottom(dark);
	const QColor bezelInk = RackPalette::GlassBezel(dark);
	const QColor bezelLip = RackPalette::GlassBezelLip(dark);
	const QColor gridMinor = dark ? QColor(tokens.graphGridMinor) : RackPalette::ScopeGridMinorLight;
	const QColor gridMajor = gridMinor.lighter(168);
	const QColor phosphor = dark ? QColor(tokens.accent2) : QColor(tokens.accent2).lighter(195);
	const QColor segmentBright = RackPalette::SegmentBright(dark);
	const QColor segmentDim = RackPalette::SegmentDim(dark);
	// The OVER voice is the danger red of a hardware PEAK lamp, not the
	// panel's amber accent: overdrive is damage. Only barely lifted on the
	// cream finish - the glass is dark in BOTH finishes, so a strong lift
	// only washes the red toward pink.
	const QColor overInk = dark ? QColor(tokens.danger) : QColor(tokens.danger).lighter(115);

	const QRectF full(state.rect);
	const qreal plateHeight = 16.0;
	const QRectF plate(full.left(), full.bottom() - plateHeight, full.width(), plateHeight);
	const QRectF glassFrame = QRectF(full).adjusted(0.5, 0.5, -0.5, -plateHeight - 0.5);

	// ── The faceplate strip under the glass ──
	painter.setRenderHint(QPainter::Antialiasing, false);
	const QColor plateColor(tokens.card);
	QLinearGradient plateSheen(plate.topLeft(), plate.bottomLeft());
	plateSheen.setColorAt(0.0, plateColor.lighter(dark ? 114 : 103));
	plateSheen.setColorAt(1.0, plateColor.darker(dark ? 110 : 105));
	painter.fillRect(plate, plateSheen);
	// The machined bottom edge: the dark rack seam under the plate.
	painter.setPen(QPen(RackPalette::Seam(dark), 1));
	painter.drawLine(state.rect.left(), state.rect.bottom(), state.rect.right(), state.rect.bottom());

	// Plate printing: engraved designation left, the footer caption centre,
	// the PEAK lamp and the function's legend window right. The designation and
	// the legend are hardware printing (never translated); the caption is
	// localized data engraved as-is - no uppercasing, no tracking.
	const QRectF plateText = plate.adjusted(10.0, 1.0, -10.0, -2.0);
	QFont plateFont(tokens.fontFamily);
	plateFont.setPixelSize(9);
	plateFont.setBold(true);
	plateFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
	const QFontMetricsF plateMetrics(plateFont);

	const qreal lampRadius = 3.0;
	const QPointF lampCenter(plateText.right() - lampRadius, plate.center().y());
	qreal reservedRight = lampRadius * 2.0 + 6.0;
	// The right slot is the function's legend window. Reading magnitude it is
	// the OVER printing beside the PEAK lamp. Reading phase or group delay
	// there is no overdrive to warn about, so the same window carries the unit
	// engraving - the axis figures are bare signed numbers in every function,
	// and without this the glass would name no unit at all. The unit string
	// comes from the state, never from a degree sign typed in here. The lamp
	// stays mounted either way: it is a component, and a component that cannot
	// light on this function is simply a dark lamp.
	const QString rightLegend = magnitude ? QStringLiteral("OVER") : state.unit;
	if (!rightLegend.isEmpty() && plateText.width() >= (magnitude ? 220.0 : 130.0))
	{
		painter.setFont(plateFont);
		const QRectF legendRect(plateText.left(), plateText.top(),
			plateText.width() - reservedRight, plateText.height());
		RackSkinDetail::engraveText(painter, legendRect, Qt::AlignRight | Qt::AlignVCenter, rightLegend,
			state.clipping ? withAlpha(overInk, 245) : withAlpha(QColor(tokens.mutedText), dark ? 140 : 180), dark);
		reservedRight += plateMetrics.horizontalAdvance(rightLegend) + 8.0;
	}

	qreal reservedLeft = 0.0;
	// The designation names the function the unit is running, the way a
	// multi-function meter's front panel does. Hardware printing, never
	// translated.
	const QString designation = magnitude
		? QStringLiteral("SPECTRUM MONITOR")
		: (state.metric == AnalysisMetric::PhaseDegrees
			? QStringLiteral("PHASE MONITOR")
			: QStringLiteral("GROUP DELAY MONITOR"));
	const qreal designationWidth = plateMetrics.horizontalAdvance(designation);
	if (plateText.width() >= designationWidth * 2.6)
	{
		painter.setFont(plateFont);
		RackSkinDetail::engraveText(painter, plateText, Qt::AlignLeft | Qt::AlignVCenter, designation,
			withAlpha(QColor(tokens.mutedText), dark ? 150 : 190), dark);
		reservedLeft = designationWidth + 14.0;
	}

	if (!state.channelText.isEmpty())
	{
		QFont captionFont(tokens.fontFamily);
		captionFont.setPixelSize(10);
		const QFontMetricsF captionMetrics(captionFont);
		const QRectF captionRect(plateText.left() + reservedLeft, plateText.top(),
			qMax(0.0, plateText.width() - reservedLeft - reservedRight), plateText.height());
		if (captionRect.width() >= 40.0)
		{
			painter.setFont(captionFont);
			RackSkinDetail::engraveText(painter, captionRect, Qt::AlignCenter,
				captionMetrics.elidedText(state.channelText, Qt::ElideRight, captionRect.width()),
				withAlpha(QColor(tokens.text), dark ? 175 : 205), dark);
		}
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	RackSkinDetail::paintLed(painter, lampCenter, lampRadius, overInk, state.clipping, dark);

	// ── The glass window ──
	QPainterPath glassPath;
	glassPath.addRoundedRect(glassFrame, 2.0, 2.0);
	QPainterStateGuard glassState(&painter);
	painter.setClipPath(glassPath);

	QLinearGradient ground(glassFrame.topLeft(), glassFrame.bottomLeft());
	ground.setColorAt(0.0, glassTop);
	ground.setColorAt(1.0, glassBottom);
	painter.fillRect(glassFrame, ground);

	// The beam's memory warms the tube; entry hover pre-heats the phosphor.
	QRadialGradient backGlow(state.plotRect.center(), qMax(1.0, state.plotRect.width() * 0.55));
	backGlow.setColorAt(0.0, withAlpha(phosphor, qRound(10.0 + 8.0 * hover)));
	backGlow.setColorAt(1.0, withAlpha(phosphor, 0));
	painter.setPen(Qt::NoPen);
	painter.setBrush(backGlow);
	painter.drawRect(state.plotRect);

	// The OVER zone: while the response can clip, the band above the 0 dB
	// axis glows danger-red under the graticule - hot at the top of the
	// glass, dying at the axis, the way an overdriven tube warns. Above the
	// axis is danger only where the axis is unity gain, so the whole red
	// vocabulary below hangs off this one flag; the magnitude test is redundant
	// with state.clipping today and kept so the fence is visible here rather
	// than assumed from a field set elsewhere.
	const qreal zeroY = state.zeroY;
	const bool overZone = magnitude && state.clipping && zeroY > state.plotRect.top() + 1.0;
	if (overZone)
	{
		QLinearGradient warn(QPointF(0.0, state.plotRect.top()), QPointF(0.0, zeroY));
		warn.setColorAt(0.0, withAlpha(overInk, 56));
		warn.setColorAt(1.0, withAlpha(overInk, 10));
		painter.fillRect(QRectF(state.plotRect.left(), state.plotRect.top(),
			state.plotRect.width(), zeroY - state.plotRect.top()), warn);
	}

	// Graticule: crisp 1px rules - straight lines carry no antialiasing (the
	// scope law shared with the GEQ display). Inside the OVER zone the rules
	// turn to the danger-red warning graticule.
	painter.setRenderHint(QPainter::Antialiasing, false);
	const int plotTop = int(state.plotRect.top());
	const int plotBottom = int(state.plotRect.bottom());
	const int plotLeft = int(state.plotRect.left());
	const int plotRight = int(state.plotRect.right());
	const int zeroRow = int(zeroY);
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		const int x = int(line.pos);
		if (overZone)
		{
			painter.setPen(QPen(withAlpha(overInk, line.major ? 120 : 78), 1));
			painter.drawLine(x, plotTop, x, zeroRow - 1);
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(x, zeroRow, x, plotBottom);
		}
		else
		{
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
			painter.drawLine(x, plotTop, x, plotBottom);
		}
	}
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		const int y = int(line.pos);
		if (overZone && y < zeroRow)
			painter.setPen(QPen(withAlpha(overInk, line.major ? 120 : 78), 1));
		else
			painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(plotLeft, y, plotRight, y);
	}

	// The zero axis: a phosphor-tinted centre line with the scope's fine
	// hash ticks - the boundary the OVER zone burns against. Struck only while
	// the metric's zero sits inside the fitted range. It is not always the
	// centre: a group delay measured upward from no delay at all keeps zero in
	// its fit, so the rail lands on the bottom of the beam area, and a phase
	// that only descends puts it on the top. This machine strikes it there
	// anyway - a rail along the floor of the tube is a scope's baseline, which
	// is exactly what zero is on those two functions.
	if (state.zeroVisible)
	{
		painter.setPen(QPen(withAlpha(phosphor, 145), 1));
		painter.drawLine(plotLeft, zeroRow, plotRight, zeroRow);
		painter.setPen(QPen(withAlpha(phosphor, 60), 1));
		for (int x = plotLeft + 4; x < plotRight - 2; x += 7)
			painter.drawLine(x, zeroRow - 2, x, zeroRow + 2);
	}

	// Axis figures: etched in segment ink (numerals - hardware printing, never
	// translated). The value column reads inside the left graticule edge and
	// takes the warning ink above the axis while the zone is hot. The figures
	// stay bare signed numbers in every function; the unit they are counted in
	// is engraved once on the plate's legend window rather than repeated down
	// the column.
	QFont axisFont(tokens.monoFontFamily);
	axisFont.setPointSizeF(8.0);
	axisFont.setBold(true);
	painter.setFont(axisFont);
	qreal figureGap = 1000.0;
	for (int i = 1; i < state.horizontal.size(); i++)
		figureGap = qMin(figureGap, qAbs(state.horizontal.at(i).pos - state.horizontal.at(i - 1).pos));
	const int labelStep = figureGap >= 13.0 ? 6 : (figureGap >= 6.5 ? 12 : 24);
	// Thinning the column. Magnitude thins on the dB ladder, because 6/12/24 dB
	// are the steps an engraved dB scale is allowed to keep. The other two
	// functions have no such ladder and the ladder actively mangles them: a
	// phase axis stepping 45 degrees loses every other figure to a modulo of 6,
	// and a millisecond figure is not a whole number at all, so it parses as
	// zero and every figure survives however tight the rows get. They thin on
	// the geometry instead, dropping rows only once they crowd.
	const int rowStride = magnitude ? 1 : qMax(1, qCeil(14.0 / qMax(1.0, figureGap)));
	int row = -1;
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
	{
		row++;
		if (line.label.isEmpty())
			continue;
		if (magnitude ? (line.label.toInt() % labelStep != 0) : (row % rowStride != 0))
			continue;
		const bool overFigure = overZone && line.pos < zeroY - 1.0;
		painter.setPen(withAlpha(overFigure ? overInk : segmentDim, line.major ? 235 : 150));
		painter.drawText(skinYTickLabelRect(int(line.pos), plotLeft + 4, 34.0, 16.0).toRect(),
			Qt::AlignLeft | Qt::AlignVCenter, line.label);
	}
	const int glassBottomRow = int(glassFrame.bottom());
	for (const AnalysisGraphState::GridLine& line : state.vertical)
	{
		if (line.label.isEmpty())
			continue;
		painter.setPen(withAlpha(segmentDim, line.major ? 235 : 150));
		painter.drawText(skinXTickLabelRect(int(line.pos), plotBottom, glassBottomRow - plotBottom).toRect(),
			Qt::AlignHCenter | Qt::AlignVCenter, line.label);
	}

	// ── The beam ── (masked to the graticule area, like a tube's trace)
	QPainterStateGuard beamState(&painter);
	painter.setClipRect(state.plotRect.adjusted(-1, -1, 1, 1), Qt::IntersectClip);
	painter.setRenderHint(QPainter::Antialiasing, true);
	// One sweep per segment. The metric goes dark where it has no value, and the
	// beam blanks with it: a tube that flew across the gap would burn in a
	// reading the machine never received.
	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;

		// Afterglow. On magnitude it is the faint phosphor wash between the
		// trace and the 0 dB axis: the area is how far the response sits from
		// unity gain, and that rail is what the beam is measured against. Phase
		// and group delay have no unity rail. Filling to their zero would report
		// nothing but the distance to a frame edge, and under phase - whose zero
		// rides the very top of the fitted range - it floods the whole tube. So
		// on those two the persistence stops being an area and becomes what a
		// slow phosphor actually leaves behind: a halo hugging the beam.
		QPolygonF afterglow;
		if (magnitude)
		{
			const double base = qBound(state.plotRect.top(), zeroY, state.plotRect.bottom());
			afterglow = segment;
			afterglow.append(QPointF(segment.last().x(), base));
			afterglow.prepend(QPointF(segment.first().x(), base));
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(phosphor, qRound(18.0 + 8.0 * hover)));
			painter.drawPolygon(afterglow);
		}
		else
		{
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(phosphor, qRound(11.0 + 6.0 * hover)), 12.0,
				Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
		}

		// The trace: glow faked by stroke overpainting (no effects on this
		// machine); the entry hover intensifies the phosphor.
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(withAlpha(phosphor, qRound(20.0 + 12.0 * hover)), 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPolyline(segment);
		painter.setPen(QPen(withAlpha(phosphor, qRound(58.0 + 22.0 * hover)), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPolyline(segment);
		painter.setPen(QPen(phosphor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPolyline(segment);

		// Above the axis the beam burns danger-red: the same passes redrawn
		// inside the OVER band only, hotter than the phosphor ever gets, plus
		// a white-hot core - an overdriven beam, not an annotation.
		if (overZone)
		{
			QPainterStateGuard overZoneState(&painter);
			painter.setClipRect(QRectF(state.plotRect.left() - 1.0, state.plotRect.top() - 1.0,
				state.plotRect.width() + 2.0, zeroY - state.plotRect.top() + 1.0), Qt::IntersectClip);
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(overInk, qRound(38.0 + 10.0 * hover)));
			painter.drawPolygon(afterglow);
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(overInk, qRound(44.0 + 14.0 * hover)), 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			painter.setPen(QPen(withAlpha(overInk, qRound(105.0 + 26.0 * hover)), 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			painter.setPen(QPen(overInk, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
			painter.setPen(QPen(withAlpha(mixColor(overInk, RackPalette::light(255), 0.55), 215), 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPolyline(segment);
		}
	}
	beamState.restore();

	// ── The measurement cursor ── a scope cursor line with grab ticks, a
	// brightened measured point on the beam and a segment readout in the
	// glass corner.
	if (state.cursorValid)
	{
		const int cursorColumn = int(state.cursor.x());
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setPen(QPen(withAlpha(segmentBright, 110), 1));
		painter.drawLine(cursorColumn, plotTop, cursorColumn, plotBottom);
		painter.setPen(QPen(withAlpha(segmentBright, 220), 1));
		painter.drawLine(cursorColumn, plotTop, cursorColumn, plotTop + 5);
		painter.drawLine(cursorColumn, plotBottom - 5, cursorColumn, plotBottom);

		painter.setRenderHint(QPainter::Antialiasing, true);
		// Overdrive is a magnitude reading, so only there does a measured point
		// above the axis burn red. On phase, whose zero rides the top of the
		// range, and on a group delay measured upward from no delay at all,
		// above the axis is where the reading ordinarily lives - a red point
		// there would report damage the filter is not doing.
		const bool overPoint = magnitude && state.curveYAtCursor < zeroY - 0.5;
		const QColor mark = overPoint ? overInk : phosphor;
		const QPointF measured(state.cursor.x(), state.curveYAtCursor);
		// The cursor line and its grab ticks stand in a null; the measured point
		// does not, because there is no reading there to brighten. The state's
		// own answer to "did this column have a value" is the prepared readout:
		// curveYAtCursor is clamped into the pane before it arrives, so it is
		// finite even where nothing was measured and cannot be tested for it. A
		// magnitude column always has a value.
		if (magnitude || !state.cursorText.isEmpty())
		{
			QRadialGradient halo(measured, 8.0);
			halo.setColorAt(0.0, withAlpha(mark, 90));
			halo.setColorAt(1.0, withAlpha(mark, 0));
			painter.setPen(Qt::NoPen);
			painter.setBrush(halo);
			painter.drawEllipse(measured, 8.0, 8.0);
			painter.setBrush(mark.lighter(130));
			painter.drawEllipse(measured, 2.6, 2.6);
		}

		if (!state.cursorText.isEmpty())
		{
			QFont readoutFont(tokens.monoFontFamily);
			readoutFont.setPointSizeF(8.5);
			readoutFont.setBold(true);
			painter.setFont(readoutFont);
			const QFontMetricsF readoutMetrics(readoutFont);
			painter.setPen(segmentBright);
			painter.drawText(QRectF(state.plotRect.adjusted(0, 3, -8, 0)), Qt::AlignRight | Qt::AlignTop,
				readoutMetrics.elidedText(state.cursorText, Qt::ElideLeft, qMax(0.0, state.plotRect.width() - 16.0)));
		}
	}

	// The bezel's overhang shadow hangs over the top of the glass - the
	// recessed grammar (shadowed top lip, lit lower lip below).
	painter.setRenderHint(QPainter::Antialiasing, true);
	QLinearGradient overhang(glassFrame.topLeft(), QPointF(glassFrame.left(), glassFrame.top() + 9.0));
	overhang.setColorAt(0.0, RackPalette::shadow(dark ? 150 : 130));
	overhang.setColorAt(1.0, RackPalette::shadow(0));
	painter.fillRect(QRectF(glassFrame.left(), glassFrame.top(), glassFrame.width(), 9.0), overhang);

	glassState.restore();

	// Bezel frame and the lit lower lip between glass and plate.
	painter.setBrush(Qt::NoBrush);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setPen(QPen(bezelInk, 1));
	painter.drawRoundedRect(glassFrame, 2.0, 2.0);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QPen(bezelLip, 1));
	painter.drawLine(int(glassFrame.left()) + 2, int(glassFrame.bottom()), int(glassFrame.right()) - 2, int(glassFrame.bottom()));
}
