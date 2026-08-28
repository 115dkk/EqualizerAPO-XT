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

// The VST3 bus contract as two friendly stadium chips: the caption lives
// inside the pill with its value ("In · Stereo · 2"), on an opaque pastel
// under deep warm ink - the ON grammar every engaged thing here wears.
// Asleep (VST2, not loaded) the chip becomes the sleeping slot: dashed
// outline, sunken to the tray, muted ink. Never an alarm.
void SoftSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QRectF pill = QRectF(state.rect).adjusted(0.5, 1.5, -0.5, -1.5);
	const qreal radius = pill.height() / 2.0;

	QColor fill = softPastelize(QColor(tokens.accent), dark);
	if (state.pressed || state.menuOpen)
		fill = mixColor(fill, QColor(tokens.text), 0.10);
	else if (state.hovered)
		fill = mixColor(fill, QColor(tokens.text), 0.06);
	QColor ink(QStringLiteral("#2B251D"));

	if (!state.enabled)
	{
		painter.setPen(QPen(QColor(tokens.border), 1, Qt::DashLine));
		painter.setBrush(QColor(tokens.surface));
		painter.drawRoundedRect(pill, radius, radius);
		ink = QColor(tokens.mutedText);
	}
	else
	{
		painter.setPen(state.focused || state.menuOpen
			? QPen(QColor(tokens.accent), 1) : QPen(Qt::NoPen));
		painter.setBrush(fill);
		painter.drawRoundedRect(pill, radius, radius);
	}

	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(9);
	QFont valueFont(tokens.fontFamily);
	valueFont.setPixelSize(11);
	valueFont.setWeight(QFont::DemiBold);

	QRectF textRect = pill.adjusted(8.0, 0, -6.0, 0);
	painter.setFont(roleFont);
	painter.setPen(withAlphaF(ink, state.enabled ? 0.72 : 0.9));
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleText);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleText);

	painter.setFont(valueFont);
	painter.setPen(ink);
	QString value = state.layoutText;
	if (state.channelCount > 0)
		value += QStringLiteral(" %1").arg(state.channelCount);
	QRectF valueRect(textRect);
	valueRect.setLeft(textRect.left() + roleWidth + 6.0);
	painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, value);

	// A soft chevron, round caps - the pill's only sharp thing is nothing.
	const QPointF caretCenter(textRect.right() - 3.0, pill.center().y() - 0.5);
	painter.setPen(QPen(withAlphaF(ink, 0.6), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	QPainterPath chevron;
	chevron.moveTo(caretCenter + QPointF(-3.2, -1.2));
	chevron.lineTo(caretCenter + QPointF(0.0, 2.0));
	chevron.lineTo(caretCenter + QPointF(3.2, -1.2));
	painter.drawPath(chevron);
}

// The joint is a rounded, unhurried arrow; the verdict a soft dot in a
// severity hue pulled toward the body ink (informing, not alarming) with a
// body-face caption - never monospace on this skin.
void SoftSkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	QColor arrowInk = withAlpha(QColor(tokens.mutedText), state.enabled ? 170 : 110);
	const qreal midY = state.jointRect.center().y() + 0.5;
	painter.setPen(QPen(arrowInk, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawLine(QPointF(state.jointRect.left() + 3.5, midY),
		QPointF(state.jointRect.right() - 3.5, midY));
	const QPointF head(state.jointRect.right() - 3.5, midY);
	painter.drawLine(head, head + QPointF(-3.5, -3.5));
	painter.drawLine(head, head + QPointF(-3.5, 3.5));

	const bool pairVerdict = !state.verdictInputText.isEmpty() || !state.verdictOutputText.isEmpty();
	const bool hasText = pairVerdict || !state.verdictText.isEmpty();
	if (state.verdictRect.isEmpty()
		|| (!hasText && state.tone == VstBusFrameState::Tone::Neutral))
		return;

	// A wordless danger verdict draws nothing here: on this skin the
	// problem lives in the reference tile (the stroke "!" transition), and
	// a red dot floating beside the chips read as a second, disorienting
	// alarm (r3 judging). The caption below explains; the chips stay calm.
	if (!hasText && state.tone == VstBusFrameState::Tone::Critical)
		return;

	const bool dark = skinIsDark(tokens);
	QColor dot(tokens.mutedText);
	switch (state.tone)
	{
	case VstBusFrameState::Tone::Success:
		dot = mixColor(QColor(tokens.success), QColor(tokens.text), 0.20);
		break;
	case VstBusFrameState::Tone::Warning:
		dot = mixColor(QColor(tokens.warning), QColor(tokens.text), dark ? 0.30 : 0.20);
		break;
	case VstBusFrameState::Tone::Critical:
		dot = mixColor(QColor(tokens.danger), QColor(tokens.text), dark ? 0.30 : 0.20);
		break;
	case VstBusFrameState::Tone::Neutral:
		dot = withAlpha(dot, 130);
		break;
	}
	if (!state.enabled)
		dot = withAlpha(dot, 140);

	const QPointF dotCenter(state.verdictRect.left() + 3.0, state.verdictRect.center().y() + 0.5);
	painter.setPen(Qt::NoPen);
	painter.setBrush(dot);
	painter.drawEllipse(dotCenter, 3.0, 3.0);

	if (!hasText)
		return;

	QFont captionFont(tokens.fontFamily);
	captionFont.setPixelSize(11);
	painter.setFont(captionFont);
	QColor ink(tokens.mutedText);
	if (state.tone == VstBusFrameState::Tone::Critical)
		ink = mixColor(QColor(tokens.danger), QColor(tokens.text), dark ? 0.40 : 0.35);
	painter.setPen(withAlpha(ink, state.enabled ? 255 : 160));
	QRectF textRect(state.verdictRect);
	textRect.setLeft(dotCenter.x() + 8.0);
	if (pairVerdict)
	{
		const QFontMetricsF metrics(captionFont);
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.verdictInputText);
		const qreal markLeft = textRect.left() + metrics.horizontalAdvance(state.verdictInputText) + 4.0;
		const qreal markY = textRect.center().y() + 0.5;
		painter.setPen(QPen(withAlpha(ink, 190), 1.3, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(markLeft, markY), QPointF(markLeft + 7.0, markY));
		painter.drawLine(QPointF(markLeft + 7.0, markY), QPointF(markLeft + 4.8, markY - 2.2));
		painter.drawLine(QPointF(markLeft + 7.0, markY), QPointF(markLeft + 4.8, markY + 2.2));
		painter.setPen(withAlpha(ink, state.enabled ? 255 : 160));
		QRectF outRect(textRect);
		outRect.setLeft(markLeft + 11.0);
		painter.drawText(outRect, Qt::AlignLeft | Qt::AlignVCenter,
			metrics.elidedText(state.verdictOutputText, Qt::ElideRight, outRect.width()));
	}
	else
	{
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetricsF(captionFont).elidedText(state.verdictText, Qt::ElideRight, textRect.width()));
	}
}

void SoftSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QRectF pill = QRectF(state.rect).adjusted(0.5, 1.5, -0.5, -1.5);
	const qreal radius = pill.height() / 2.0;

	// An OUTLINED pill, unlike the bus selector's filled accent pill: the
	// fill cells are the quieter, many-of-them controls. A dash slot wears
	// the dashed outline the skin already uses for not-there things.
	QColor ink(QStringLiteral("#2B251D"));
	if (dark)
		ink = QColor(tokens.text);
	QColor border = softPastelize(QColor(tokens.accent), dark);
	if (state.missingChannel)
		border = QColor(tokens.danger);
	QPen borderPen(border, 1);
	if (state.silent)
		borderPen.setStyle(Qt::DashLine);
	if (state.enabled && (state.focused || state.menuOpen))
		borderPen.setWidthF(1.6);
	painter.setPen(borderPen);
	QColor fill(tokens.surface);
	if (state.enabled && (state.pressed || state.menuOpen))
		fill = mixColor(fill, border, 0.18);
	else if (state.enabled && state.hovered)
		fill = mixColor(fill, border, 0.10);
	painter.setBrush(fill);
	painter.drawRoundedRect(pill, radius, radius);

	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(9);
	QFont valueFont(tokens.fontFamily);
	valueFont.setPixelSize(11);
	valueFont.setWeight(QFont::DemiBold);

	QRectF textRect = pill.adjusted(8.0, 0, -6.0, 0);
	painter.setFont(roleFont);
	painter.setPen(withAlphaF(ink, state.enabled ? 0.62 : 0.4));
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleToken);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);

	painter.setFont(valueFont);
	QColor valueInk = withAlphaF(ink, state.enabled ? (state.silent || state.defaulted ? 0.55 : 0.95) : 0.4);
	if (state.missingChannel)
		valueInk = QColor(tokens.danger);
	painter.setPen(valueInk);
	painter.drawText(QRectF(textRect.left() + roleWidth + 5.0, textRect.top(),
		textRect.width() - roleWidth - 5.0 - 8.0, textRect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, state.valueText);

	const qreal caretHalf = 2.5;
	const QPointF caretMid(textRect.right() - 3.0, pill.center().y() + 0.5);
	QPainterPath caret;
	caret.moveTo(caretMid + QPointF(-caretHalf, -caretHalf / 2.0));
	caret.lineTo(caretMid + QPointF(caretHalf, -caretHalf / 2.0));
	caret.lineTo(caretMid + QPointF(0.0, caretHalf));
	caret.closeSubpath();
	painter.fillPath(caret, withAlphaF(ink, state.enabled ? 0.5 : 0.3));
}

void SoftSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QColor pastel = softPastelize(QColor(tokens.accent), dark);

	// The tray: a soft rounded wash holding the pills together as one
	// gesture of the card, not a scatter of chips.
	if (!state.cellsRect.isNull() && !state.collapsed)
	{
		const QRectF tray = QRectF(state.cellsRect).adjusted(-6.0, -3.0, 6.0, 3.0);
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlphaF(pastel, dark ? 0.10 : 0.22));
		painter.drawRoundedRect(tray, tray.height() / 2.0, tray.height() / 2.0);
	}

	if (state.latchRect.isNull())
		return;
	// The fold is a little toggle: a dot that fills while the tray is out.
	const QRectF latch(state.latchRect);
	QColor ink(dark ? QColor(tokens.text) : QColor(QStringLiteral("#2B251D")));
	const QPointF dotCenter(latch.left() + 7.0, latch.center().y() + 0.5);
	painter.setPen(QPen(state.latchFocused ? QColor(tokens.accent) : withAlphaF(ink, 0.45), 1.2));
	painter.setBrush(state.collapsed ? QBrush(Qt::NoBrush) : QBrush(pastel));
	painter.drawEllipse(dotCenter, 4.0, 4.0);
	QFont latchFont(tokens.fontFamily);
	latchFont.setPixelSize(10);
	if (state.latchHovered)
		latchFont.setUnderline(true);
	painter.setFont(latchFont);
	painter.setPen(withAlphaF(ink, state.collapsed ? 0.55 : 0.9));
	painter.drawText(QRectF(latch.left() + 15.0, latch.top(), latch.width() - 15.0, latch.height()),
		Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Fill"));
}
