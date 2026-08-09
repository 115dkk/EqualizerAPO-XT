/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

// A row of mutually exclusive choices, in the grammar this skin already
// owns for "pick one of these": the matched bank of equal-width stadium
// pills (the Stage card's switch bank, the Phase 2 pill states). The bank
// is bound into one object by a sunken track - the value-scrub well's
// ground - so it reads as one setting with three positions instead of
// three toggles that happen to sit together, and the choice itself wears
// the ON grammar every switched-on thing here wears: an opaque pastel
// fill under deep warm ink.
//
// The pill TRAVELS. It is drawn at selectionPosition, and each label's ink
// warms in proportion to how much of the pill has arrived over its cell,
// so running through three choices is one pastel object walking to its
// next slot rather than three cells blinking. Sliding is the settings-app
// gesture this skin is modelled on; switching belongs to the skins with
// harder edges. One control for both of its uses (the analysis metric, an
// all-pass's order) - a second convention for the same job would be a
// second thing to learn.
void SoftSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	if (state.labels.isEmpty())
		return;

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor accent(tokens.accent);
	const QColor warmInk(QStringLiteral("#2B251D"));
	const bool asleep = !state.enabled;

	// The ground: one elevation step down, closed by the very light 1px
	// border. No shadow, ever. Asleep it is the sleeping-slot triple -
	// sunk into the window background, dashed outline, muted ink - which
	// is an empty slot, not an alarm.
	const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal trackRadius = frame.height() / 2.0;
	painter.setPen(Qt::NoPen);
	painter.setBrush(asleep ? QColor(tokens.background) : QColor(tokens.surfaceSunken));
	painter.drawRoundedRect(frame, trackRadius, trackRadius);

	// Roomy on every side, so the mark is a stadium inside a stadium and
	// the cells keep a visible gap between them. Nothing divides the cells
	// but that gap - dividing hairlines are the neighbours' vocabulary.
	const qreal inset = qBound(2.0, frame.height() / 7.0, 4.0);
	const auto pillOf = [&](double index) {
		return state.segmentRect(index).adjusted(inset, inset, -inset, -inset);
	};
	const QRectF mark = pillOf(state.selectionPosition);
	const qreal pillRadius = mark.height() / 2.0;

	// Hover on a cell that is not the choice: exactly one step up from the
	// sunken ground, with the scrub well's pale accent edge. The step
	// alone is a few units of lightness in the light theme, which is the
	// kind of state that passes review and is invisible on a real panel,
	// so the edge carries it there.
	if (!asleep && state.hoveredIndex >= 0 && state.hoveredIndex != state.selectedIndex
		&& state.hoveredIndex != state.pressedIndex)
	{
		const QRectF hoverPill = pillOf(state.hoveredIndex);
		painter.setPen(QPen(withAlpha(accent, 128), 1));
		painter.setBrush(QColor(tokens.surface));
		painter.drawRoundedRect(hoverPill, pillRadius, pillRadius);
	}

	// Pressed on another cell: the knob's always-visible track pastel (the
	// scope arm's resting mix), the choice on its way but not yet made -
	// the release makes it. It stays a step below the ON pill on purpose,
	// so a press never competes with the current choice for the eye.
	// Pressing the current choice deepens its own pastel instead, the ON
	// ladder the add row's disc climbs.
	if (!asleep && state.pressedIndex >= 0 && state.pressedIndex != state.selectedIndex)
	{
		const QRectF pressPill = pillOf(state.pressedIndex);
		painter.setPen(Qt::NoPen);
		painter.setBrush(mixColor(accent, QColor(tokens.card), 0.78));
		painter.drawRoundedRect(pressPill, pillRadius, pillRadius);
	}

	QColor markFill = accent;
	if (asleep)
		markFill = mixColor(accent, QColor(tokens.background), 0.62);
	else if (state.pressedIndex == state.selectedIndex)
		markFill = mixColor(accent, warmInk, 0.18);
	painter.setPen(Qt::NoPen);
	painter.setBrush(markFill);
	painter.drawRoundedRect(mark, pillRadius, pillRadius);

	QFont font = painter.font();
	font.setWeight(QFont::DemiBold);
	painter.setFont(font);
	const QFontMetricsF metrics(font);
	for (int i = 0; i < state.labels.size(); i++)
	{
		const QRectF cell = state.segmentRect(i);
		// How much of the travelling pill has arrived over this cell: 1 on
		// the chosen cell at rest, 0 everywhere else, and split between
		// two cells while it walks. The ink crosses over exactly as the
		// pastel does, so the label is never dark warm ink on the ground.
		const double arrival = mark.width() > 0.0
			? qBound(0.0, mark.intersected(cell).width() / mark.width(), 1.0)
			: 0.0;
		QColor resting(tokens.mutedText);
		if (!asleep && (i == state.hoveredIndex || i == state.pressedIndex))
			resting = QColor(tokens.text);
		painter.setPen(asleep ? QColor(tokens.mutedText) : mixColor(resting, warmInk, arrival));
		painter.drawText(cell, Qt::AlignCenter,
			metrics.elidedText(state.labels.at(i), Qt::ElideRight,
				int(qMax(8.0, cell.width() - inset * 2.0 - 6.0))));
	}

	// Focus is the quiet halo (alpha 90, 3px) hugging the inside of the
	// track, never a hard ring.
	if (state.focused && !asleep)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(frame.adjusted(1.5, 1.5, -1.5, -1.5),
			qMax(0.0, trackRadius - 1.5), qMax(0.0, trackRadius - 1.5));
	}

	QPen edge(QColor(tokens.border), 1);
	if (asleep)
		edge.setStyle(Qt::DashLine);
	painter.setPen(edge);
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(frame, trackRadius, trackRadius);
}

// "A handle you cannot fumble." The largest knob of the five skins:
// two-step elevation body, rounded dot indicator, value in a rounded
// badge below, always-visible pastel track ring.
void SoftSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor card(tokens.card);
	const QColor windowBg(tokens.background);
	const QColor border(tokens.border);
	const QColor muted(tokens.mutedText);

	// Reserve a strip at the bottom for the rounded value badge so it sits
	// below the handle instead of floating on the face. Promoted legacy
	// dials hand in an empty valueText (their value lives in a spin box),
	// so they keep the full height for the handle.
	const bool hasBadge = !state.valueText.isEmpty();
	QRectF area(rect);
	qreal badgeHeight = 0;
	if (hasBadge)
	{
		badgeHeight = qMin<qreal>(18.0, area.height() * 0.26);
		area.setBottom(area.bottom() - badgeHeight - 1.0);
	}

	// Largest knob of the five: only a 4px inset, centred square so the
	// handle stays circular in the 100x66 legacy dial slots.
	QRectF inner = area.adjusted(4, 4, -4, -4);
	const double side = qMin(inner.width(), inner.height());
	QRectF knobRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);

	const int spanDegrees = 270;
	const int startDegrees = 135;
	const double ratio = qBound(0.0, state.ratio, 1.0);
	const double endDegrees = startDegrees + spanDegrees * ratio;

	const double arcWidth = qMax(5.0, side * 0.10);
	QRectF arcRect = knobRect.adjusted(arcWidth / 2.0, arcWidth / 2.0, -arcWidth / 2.0, -arcWidth / 2.0);

	// Keyboard focus: a quiet halo around the whole handle, not a hard ring.
	if (state.focused && state.enabled)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(knobRect.adjusted(-2, -2, 2, 2));
	}

	// Always-visible pastel track ring. Unipolar travel wears one
	// accent pastel; a bipolar knob splits at the 12 o'clock detent into
	// an accent2 cut half and an accent boost half, so gain reads as
	// two-sided even while it rests at 0 dB.
	const double centerDegrees = startDegrees + spanDegrees / 2.0;
	if (state.enabled && state.bipolar)
	{
		painter.setPen(QPen(mixColor(QColor(tokens.accent2), card, 0.78), arcWidth, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees / 2.0 * 16.0));
		painter.setPen(QPen(mixColor(QColor(tokens.accent), card, 0.78), arcWidth, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-spanDegrees / 2.0 * 16.0));
	}
	else
	{
		const QColor trackColor = state.enabled ? mixColor(QColor(tokens.accent), card, 0.80) : withAlpha(border, 110);
		painter.setPen(QPen(trackColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(arcRect, -startDegrees * 16, -spanDegrees * 16);
	}

	// Pastel value arc (accent softened one step toward the card colour).
	if (state.enabled)
	{
		const bool cutSide = state.bipolar && ratio < 0.5;
		const QColor valueColor = mixColor(QColor(cutSide ? tokens.accent2 : tokens.accent), card, 0.25);
		painter.setPen(QPen(valueColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
		if (state.bipolar)
		{
			painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-(endDegrees - centerDegrees) * 16.0));
		}
		else
		{
			painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees * ratio * 16.0));
		}
	}

	// The 0 dB detent is a soft rounded tick crossing the track ring
	// at 12 o'clock, painted over the value arc so the neutral point stays
	// marked however far the knob is turned. Only bipolar (gain) knobs
	// carry it - one more way the two knob kinds differ at a glance.
	if (state.bipolar)
	{
		const QPointF arcCenter = arcRect.center();
		const double trackRadius = arcRect.width() / 2.0;
		painter.setPen(QPen(withAlpha(QColor(tokens.text), state.enabled ? 200 : 90), 2.5, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(arcCenter.x(), arcCenter.y() - trackRadius - arcWidth / 2.0 + 0.5),
			QPointF(arcCenter.x(), arcCenter.y() - trackRadius + arcWidth / 2.0 - 0.5));
	}

	// Two-step elevation body: a base disc one value step below the face,
	// then the face one step above with a very light 1px border. Hover
	// lifts the face exactly one value step; no real shadow effects.
	const double faceInset = arcWidth + 2.5;
	QRectF baseRect = knobRect.adjusted(faceInset, faceInset, -faceInset, -faceInset);
	painter.setPen(Qt::NoPen);
	painter.setBrush(mixColor(card, windowBg, 0.55));
	painter.drawEllipse(baseRect);

	QColor faceColor = card;
	if (!state.enabled)
		faceColor = mixColor(card, windowBg, 0.5);
	else if (state.hovered || state.dragging)
		faceColor = QColor(tokens.cardHover);
	QRectF faceRect = baseRect.adjusted(2.5, 2.5, -2.5, -2.5);
	painter.setPen(QPen(border, 1));
	painter.setBrush(faceColor);
	painter.drawEllipse(faceRect);

	// Rounded dot indicator instead of a sharp line; it grows slightly on
	// hover and again while dragging, the calmest possible "I am held"
	// cue. The dot is large enough that the position reads from across
	// the row, and on a bipolar knob it takes the colour of the side it
	// sits on (accent boost, accent2 cut).
	double dotRadius = qMax(4.5, side * 0.085);
	if (state.dragging)
		dotRadius += 1.0;
	else if (state.hovered)
		dotRadius += 0.5;
	const double dotTrack = faceRect.width() / 2.0 - dotRadius - 2.5;
	const double radians = qDegreesToRadians(-endDegrees);
	const QPointF dotPos(faceRect.center().x() + qCos(radians) * dotTrack,
		faceRect.center().y() - qSin(radians) * dotTrack);
	painter.setPen(Qt::NoPen);
	const QColor dotColor(state.bipolar && ratio < 0.5 ? tokens.accent2 : tokens.accent);
	painter.setBrush(state.enabled ? dotColor : withAlpha(muted, 120));
	painter.drawEllipse(dotPos, dotRadius, dotRadius);

	// Value in a rounded badge below the handle.
	if (hasBadge)
	{
		QFont badgeFont = painter.font();
		badgeFont.setWeight(QFont::DemiBold);
		badgeFont.setPointSizeF(qMax(7.0, badgeFont.pointSizeF() - 1.5));
		painter.setFont(badgeFont);
		const QFontMetricsF metrics(badgeFont);
		const qreal badgeWidth = qMin<qreal>(QRectF(rect).width(), metrics.horizontalAdvance(state.valueText) + 14.0);
		QRectF badgeRect(QRectF(rect).center().x() - badgeWidth / 2.0,
			QRectF(rect).bottom() - badgeHeight - 0.5, badgeWidth, badgeHeight);
		painter.setPen(QPen(border, 1));
		painter.setBrush(state.enabled ? QColor(tokens.surfaceRaised) : mixColor(card, windowBg, 0.5));
		painter.drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);
		painter.setPen(state.enabled ? QColor(tokens.text) : muted);
		painter.drawText(badgeRect, Qt::AlignCenter, state.valueText);
	}
}
