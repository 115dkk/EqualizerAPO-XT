/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "MatrixSkinDetail.h"

void MatrixSkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
		const QColor ground(tokens.graph);
		const QColor borderInk(tokens.border);
		const QColor mutedInk(tokens.mutedText);
		const QColor textInk(tokens.text);
		const QColor accent(tokens.accent);
		const bool darkBoard = tokens.dark;
		// Caution ink: full amber only on the dark board. On the light board
		// raw orange reads as crayon against the ice palette, so it sinks to
		// a printed ochre - hue kept, saturation and value derived down.
		const QColor warnBase(tokens.warning);
		const QColor hazardInk = darkBoard ? warnBase
			: QColor::fromHsvF(warnBase.hsvHueF(), warnBase.hsvSaturationF() * 0.82, warnBase.valueF() * 0.62);
		const QRect plot = state.plotRect.toRect();

		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.fillRect(state.rect, ground);

		// Crisp 1px grid, integer-aligned; same rank derivation as the
		// GraphicEQ plot.
		QColor majorInk(mutedInk);
		majorInk.setAlpha(90);
		const QColor minorInk(tokens.graphGridMinor);
		// Unwrapped phase runs past half turns, and on this board a half turn
		// is a landmark: a rule standing on a multiple of 180 degrees takes the
		// major rank even though the axis marks only zero as major, so an
		// all-pass sweep reads as counted turns instead of an even ladder.
		// Magnitude and group delay have no such landmark and keep the ranks
		// they arrive with. The value behind a rule is recovered from its y
		// through the same mapping the widget used to place it.
		const double valueSpan = state.maximum - state.minimum;
		const auto landmarkRule = [&state, valueSpan](const AnalysisGraphState::GridLine& line) {
			if (state.metric != AnalysisMetric::PhaseDegrees || valueSpan <= 0.0
				|| state.plotRect.height() <= 0.0)
				return false;
			const double value = state.maximum
				- (line.pos - state.plotRect.top()) / state.plotRect.height() * valueSpan;
			const double turns = value / 180.0;
			return qAbs(turns - qRound(turns)) < 1e-6;
		};
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			const int x = int(line.pos);
			painter.setPen(QPen(line.major ? majorInk : minorInk, 1));
			painter.drawLine(x, plot.top(), x, plot.bottom());
		}
		for (const AnalysisGraphState::GridLine& line : state.horizontal)
		{
			const int y = int(line.pos);
			painter.setPen(QPen(line.major || landmarkRule(line) ? majorInk : minorInk, 1));
			painter.drawLine(plot.left(), y, plot.right(), y);
		}

		// Hazard zone: the response can clip, so the whole over-bus band
		// posts thin amber diagonals (AA off - the pixel staircase is
		// deliberate).
		const int zeroYpx = int(state.zeroY);
		if (state.clipping && zeroYpx > plot.top())
		{
			const QRect zone(plot.left(), plot.top(), plot.width(), zeroYpx - plot.top());
			QColor hatch(hazardInk);
			hatch.setAlpha(darkBoard ? 60 : 70);
			QPainterStateGuard hazardState(&painter);
			painter.setClipRect(zone);
			painter.setPen(QPen(hatch, 1));
			// Diagonals on the board's 12px half-pitch (gridPitch 24 = two
			// rows of 12): thin rules that read individually, not a texture.
			for (int x = zone.left() - zone.height(); x <= zone.right(); x += 12)
				painter.drawLine(x, zone.bottom(), x + zone.height(), zone.top());

			// Where the trace actually exceeds the bus, the hazard densifies:
			// half-pitch diagonals at full caution ink, clipped to the area
			// between the trace and the 0 dB rule. The band says "this side
			// can clip"; the dense region says WHERE and BY HOW MUCH. One
			// closed cell per segment, cut in a single stencil, so the dense
			// ink lands only where the board actually posted a reading.
			QPainterPath overshoot;
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
			if (!overshoot.isEmpty())
			{
				painter.setClipPath(overshoot, Qt::IntersectClip);
				QColor dense(hazardInk);
				dense.setAlpha(darkBoard ? 165 : 190);
				painter.setPen(QPen(dense, 1));
				for (int x = zone.left() - zone.height(); x <= zone.right(); x += 5)
					painter.drawLine(x, zone.bottom(), x + zone.height(), zone.top());
			}
		}

		// The zero bus: the board's reference rule, one rank of authority
		// above the grid. Posted only when the metric's zero sits inside the
		// fitted range, and - for the metrics that can land it there - only
		// when it is clear of the frame: a group delay that never goes
		// negative and a phase that never rises above zero both push it onto
		// the outer rule, where the same 1px line is a border and not a bus.
		// Magnitude fits symmetrically, so its bus is always inside the pane
		// and this second test can never fire on it.
		const bool zeroOnFrame = state.metric != AnalysisMetric::MagnitudeDb
			&& (zeroYpx <= plot.top() + 1 || zeroYpx >= plot.bottom() - 1);
		if (state.zeroVisible && !zeroOnFrame)
		{
			QColor zeroInk(textInk);
			zeroInk.setAlpha(180);
			painter.setPen(QPen(zeroInk, 1));
			painter.drawLine(plot.left(), zeroYpx, plot.right(), zeroYpx);
		}

		// Phase and group delay have no value at all inside a null, so the
		// trace arrives in pieces. A departure board does not leave a slot
		// blank: the columns with no reading are bracketed by the cancellation
		// dash (form, never colour - a null is a reading the board could not
		// take, not a fault) and, where the gap is wide enough to carry it,
		// posted with the picker's empty-scan wording in a sunken mono cell.
		// Magnitude always arrives as one segment, so none of this is on its
		// path.
		if (state.metric != AnalysisMetric::MagnitudeDb)
		{
			QFont gapFont(tokens.monoFontFamily);
			gapFont.setPointSizeF(8.0);
			gapFont.setBold(true);
			const QFontMetrics gapMetrics(gapFont);
			const QString gapText = QStringLiteral("NO SIGNAL");
			const int gapCellWidth = gapMetrics.horizontalAdvance(gapText) + 12;
			const int gapCellHeight = gapMetrics.height() + 2;
			const auto postGap = [&](double from, double to) {
				const int left = qRound(from);
				const int right = qRound(to);
				// A one or two column hole is already legible as a break in
				// the trace; bracketing it would print two dashes on top of
				// each other.
				if (right - left < 3)
					return;
				painter.setPen(QPen(borderInk, 1, Qt::DashLine));
				if (left > plot.left())
					painter.drawLine(left, plot.top() + 1, left, plot.bottom() - 1);
				if (right < plot.right())
					painter.drawLine(right, plot.top() + 1, right, plot.bottom() - 1);
				if (right - left < gapCellWidth + 10)
					return;
				const QRect gapRect((left + right - gapCellWidth) / 2,
					plot.center().y() - gapCellHeight / 2, gapCellWidth, gapCellHeight);
				painter.setPen(QPen(borderInk, 1));
				painter.setBrush(QColor(tokens.surfaceSunken));
				painter.drawRect(gapRect.adjusted(0, 0, -1, -1));
				painter.setBrush(Qt::NoBrush);
				painter.setFont(gapFont);
				painter.setPen(mutedInk);
				painter.drawText(gapRect, Qt::AlignCenter, gapText);
			};
			// The complement of what the segments cover, so a hole at either
			// end of the axis is posted the same way as one in the middle.
			double coveredTo = state.plotRect.left();
			for (const QPolygonF& segment : state.curves)
			{
				if (segment.isEmpty())
					continue;
				postGap(coveredTo, segment.first().x());
				coveredTo = qMax(coveredTo, segment.last().x());
			}
			postGap(coveredTo, state.plotRect.right());
		}

		// Mono axis figures in tag cells punched out of the grid (ground fill
		// under the figure). Majors speak muted ink, minors one step quieter.
		// Frequency tags ride the bottom edge; a tag that would collide with
		// its neighbour is skipped, never squeezed.
		QFont tagFont(tokens.monoFontFamily);
		tagFont.setPointSizeF(8.0);
		const QFontMetrics tagMetrics(tagFont);
		const int tagHeight = tagMetrics.height();
		QColor minorLabelInk(mutedInk);
		minorLabelInk.setAlpha(150);
		painter.setFont(tagFont);
		int lastTagRight = state.rect.left() - 100;
		for (const AnalysisGraphState::GridLine& line : state.vertical)
		{
			if (line.label.isEmpty())
				continue;
			const int tagWidth = tagMetrics.horizontalAdvance(line.label) + 6;
			int tagX = int(line.pos) - tagWidth / 2;
			tagX = qBound(state.rect.left() + 1, tagX, state.rect.right() - tagWidth - 1);
			if (tagX <= lastTagRight + 4)
				continue;
			const QRect tagRect(tagX, plot.bottom() - tagHeight - 1, tagWidth, tagHeight);
			painter.fillRect(tagRect, ground);
			painter.setPen(line.major ? mutedInk : minorLabelInk);
			painter.drawText(tagRect, Qt::AlignCenter, line.label);
			lastTagRight = tagRect.right();
		}

		// Value tags ride the left edge. When the fitted range packs the value
		// rules tighter than a figure, thin the tags anchored on the zero bus
		// so the bus always keeps its figure. A tag that cannot centre on its
		// rule inside the plot (the range extremes at the plot edges) is
		// dropped, not squeezed - the footer's span readout posts those two
		// figures, and a tag off its rule would lie about its coordinate.
		int labelStep = 1;
		if (state.horizontal.size() >= 2)
		{
			const double spacing = qAbs(state.horizontal.at(1).pos - state.horizontal.at(0).pos);
			if (spacing > 0.5)
				labelStep = qMax(1, qCeil((tagHeight + 3) / spacing));
		}
		int zeroIndex = 0;
		for (int i = 0; i < state.horizontal.size(); i++)
		{
			if (state.horizontal.at(i).major)
			{
				zeroIndex = i;
				break;
			}
		}
		for (int i = 0; i < state.horizontal.size(); i++)
		{
			const AnalysisGraphState::GridLine& line = state.horizontal.at(i);
			if (line.label.isEmpty() || (i - zeroIndex) % labelStep != 0)
				continue;
			const int tagWidth = tagMetrics.horizontalAdvance(line.label) + 6;
			const int tagY = int(line.pos) - tagHeight / 2;
			if (tagY < plot.top() + 1 || tagY + tagHeight > plot.bottom() - 1)
				continue;
			const QRect tagRect(plot.left() + 4, tagY, tagWidth, tagHeight);
			painter.fillRect(tagRect, ground);
			// The tag keeps the rank of the rule it stands on, so a promoted
			// half-turn landmark is named as loudly as it is drawn.
			painter.setPen(line.major || landmarkRule(line) ? mutedInk : minorLabelInk);
			painter.drawText(tagRect, Qt::AlignCenter, line.label);
		}

		// The response trace: an accent core over a single wider low-alpha
		// echo stroke. The curve is data, so it alone is antialiased. One run
		// of the stroke pair per segment - the trace breaks where the metric
		// has no reading, and a stroke bridging the break would post a figure
		// the board never measured.
		for (const QPolygonF& segment : state.curves)
		{
			if (segment.size() < 2)
				continue;
			painter.setRenderHint(QPainter::Antialiasing, true);
			QColor echo(accent);
			echo.setAlpha(70);
			painter.setPen(QPen(echo, 3));
			painter.drawPolyline(segment);
			painter.setPen(QPen(accent, 1));
			painter.drawPolyline(segment);
			painter.setRenderHint(QPainter::Antialiasing, false);
		}

		// The clip peak wears an OVER tag: a boxed cell in caution amber
		// pinned to the highest point of the trace, tied down by a 1px tick.
		// The scan runs across every segment - one tag for the whole board,
		// on the loudest point wherever it landed.
		if (state.clipping)
		{
			QPointF peak;
			bool peakFound = false;
			for (const QPolygonF& segment : state.curves)
			{
				if (segment.size() < 2)
					continue;
				for (const QPointF& point : segment)
				{
					if (!peakFound || point.y() < peak.y())
					{
						peak = point;
						peakFound = true;
					}
				}
			}
			if (peakFound && peak.y() < state.zeroY)
			{
				QFont overFont(tokens.monoFontFamily);
				overFont.setPointSizeF(8.0);
				overFont.setBold(true);
				const QFontMetrics overMetrics(overFont);
				const QString overText = QStringLiteral("OVER");
				const int overWidth = overMetrics.horizontalAdvance(overText) + 8;
				const int overHeight = overMetrics.height() + 2;
				int overX = qRound(peak.x()) - overWidth / 2;
				overX = qBound(plot.left() + 2, overX, plot.right() - overWidth - 2);
				int overY = qRound(peak.y()) - 5 - overHeight;
				bool above = true;
				if (overY < plot.top() + 2)
				{
					overY = qRound(peak.y()) + 5;
					above = false;
				}
				const QRect overRect(overX, overY, overWidth, overHeight);
				painter.setPen(QPen(hazardInk, 1));
				painter.setBrush(QColor(tokens.surfaceSunken));
				painter.drawRect(overRect.adjusted(0, 0, -1, -1));
				painter.setBrush(Qt::NoBrush);
				painter.setFont(overFont);
				painter.drawText(overRect, Qt::AlignCenter, overText);
				const int tickX = qBound(overRect.left() + 1, qRound(peak.x()), overRect.right() - 1);
				if (above)
					painter.drawLine(tickX, overRect.bottom() + 1, tickX, qRound(peak.y()) - 2);
				else
					painter.drawLine(tickX, overRect.top() - 1, tickX, qRound(peak.y()) + 2);
			}
		}

		// Cursor: the scan rule energizes from dim to full with the widget's
		// hover progress; a square corner-bracket reticle marks where it
		// crosses the trace, and the reading slides with the pointer in a
		// boxed sunken mono cell.
		if (state.cursorValid)
		{
			const int scanX = qBound(plot.left(), qRound(state.cursor.x()), plot.right());
			QColor scanInk(accent);
			scanInk.setAlpha(90 + qRound(state.hover * 165.0));
			painter.setPen(QPen(scanInk, 1));
			painter.drawLine(scanX, plot.top(), scanX, plot.bottom());

			// Target acquired, but only where there is a target: a column the
			// metric has no value for hands over a clamped y, and a reticle
			// pinned to the frame edge would claim a crossing the response
			// never had. The scan rule stays (the board is still addressing
			// that column) and the gap posting above says why nothing is
			// there. Magnitude always has a reading, so it always brackets.
			const bool noReading = state.metric != AnalysisMetric::MagnitudeDb
				&& state.cursorText.isEmpty();
			if (!noReading)
			{
				const int crossY = qRound(state.curveYAtCursor);
				const int bracketLeft = scanX - 5;
				const int bracketRight = scanX + 5;
				const int bracketTop = crossY - 5;
				const int bracketBottom = crossY + 5;
				const int leg = 3;
				QColor reticleInk(accent);
				reticleInk.setAlpha(140 + qRound(state.hover * 115.0));
				painter.setPen(QPen(reticleInk, 1));
				painter.drawLine(bracketLeft, bracketTop, bracketLeft + leg, bracketTop);
				painter.drawLine(bracketLeft, bracketTop, bracketLeft, bracketTop + leg);
				painter.drawLine(bracketRight - leg, bracketTop, bracketRight, bracketTop);
				painter.drawLine(bracketRight, bracketTop, bracketRight, bracketTop + leg);
				painter.drawLine(bracketLeft, bracketBottom - leg, bracketLeft, bracketBottom);
				painter.drawLine(bracketLeft, bracketBottom, bracketLeft + leg, bracketBottom);
				painter.drawLine(bracketRight, bracketBottom - leg, bracketRight, bracketBottom);
				painter.drawLine(bracketRight - leg, bracketBottom, bracketRight, bracketBottom);
			}

			if (!state.cursorText.isEmpty())
			{
				QFont probeFont(tokens.monoFontFamily);
				probeFont.setPointSizeF(8.5);
				probeFont.setBold(true);
				const QFontMetrics probeMetrics(probeFont);
				const int probeWidth = probeMetrics.horizontalAdvance(state.cursorText) + 12;
				int probeX = scanX - probeWidth / 2;
				probeX = qBound(plot.left() + 2, probeX, plot.right() - probeWidth - 2);
				const QRect probeRect(probeX, plot.top() + 4, probeWidth, MatrixMetrics::knobCellHeight);
				painter.setPen(QPen(mixColor(borderInk, accent, state.hover), 1));
				painter.setBrush(QColor(tokens.surfaceSunken));
				painter.drawRect(probeRect.adjusted(0, 0, -1, -1));
				painter.setBrush(Qt::NoBrush);
				painter.setFont(probeFont);
				painter.setPen(textInk);
				painter.drawText(probeRect, Qt::AlignCenter, state.cursorText);
			}
		}

		// Terse board caption in the masthead margin (a painted stylistic
		// caption in the toolbar caption grammar, not user data). A board
		// names what it is carrying, and the metric switch changes exactly
		// that, so the two new quantities take the masthead with the unit in
		// brackets behind them - the unit arrives finished in the state and is
		// only put in board case here. Magnitude keeps the designation it has
		// always had.
		QString masthead = QStringLiteral("RESPONSE");
		if (state.metric != AnalysisMetric::MagnitudeDb)
		{
			const QString quantity = state.metric == AnalysisMetric::PhaseDegrees
				? QStringLiteral("PHASE")
				: QStringLiteral("GROUP DELAY");
			masthead = state.unit.isEmpty()
				? quantity
				: QStringLiteral("%1 [%2]").arg(quantity, state.unit.toUpper());
		}
		QFont captionFont(tokens.monoFontFamily);
		captionFont.setPointSizeF(8.0);
		captionFont.setWeight(QFont::DemiBold);
		captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		painter.setFont(captionFont);
		painter.setPen(mutedInk);
		painter.drawText(QRect(plot.left(), state.rect.top() + 2, qMax(0, plot.width()), 12),
			Qt::AlignLeft | Qt::AlignVCenter, masthead);

		// Footer: a sunken board line under a 1px rule. "> " marker, then the
		// prepared channel/sample-rate caption exactly as handed over
		// (localized data, elided when tight), lit from muted to body ink by
		// hover; the fitted span reads on the right.
		const int footerTop = state.rect.bottom() - 17;
		painter.fillRect(QRect(state.rect.left() + 1, footerTop + 1, state.rect.width() - 2, 16), QColor(tokens.surfaceSunken));
		painter.setPen(QPen(borderInk, 1));
		painter.drawLine(state.rect.left() + 1, footerTop, state.rect.right() - 1, footerTop);

		QFont footerFont(tokens.monoFontFamily);
		footerFont.setPointSizeF(8.5);
		painter.setFont(footerFont);
		const QFontMetrics footerMetrics(footerFont);
		const QRect footerRect(state.rect.left() + 10, footerTop + 1, state.rect.width() - 20, 16);
		// The span arrives finished, in the metric's own unit; the board only
		// puts it in board case.
		const QString spanText = state.spanValueText.toUpper();
		painter.setPen(mutedInk);
		painter.drawText(footerRect, Qt::AlignRight | Qt::AlignVCenter, spanText);
		const QString marker = QStringLiteral("> ");
		painter.drawText(footerRect, Qt::AlignLeft | Qt::AlignVCenter, marker);
		const int channelX = footerRect.left() + footerMetrics.horizontalAdvance(marker);
		const int channelAvail = footerRect.right() - footerMetrics.horizontalAdvance(spanText) - 12 - channelX;
		painter.setPen(mixColor(mutedInk, textInk, state.hover));
		painter.drawText(QRect(channelX, footerRect.top(), qMax(0, channelAvail), footerRect.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			footerMetrics.elidedText(state.channelText, Qt::ElideRight, qMax(0, channelAvail)));

		// The data cell's outer 1px rule.
		painter.setPen(QPen(borderInk, 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
	}
