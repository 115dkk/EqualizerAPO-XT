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
// owns for "pick one of these": free-standing stadium pills on the paper
// with a visible gap between them (nothing divides them but that gap),
// and the choice wearing the chosen-pill grammar every picked thing here
// wears since the concept swap: the pale tint under accent ink, edged by
// the card mixed toward the accent. No sunken track - the mockup's
// segment is three pills, not a switch bank.
//
// The pill TRAVELS. It is drawn at selectionPosition, and each label's ink
// crosses to the accent in proportion to how much of the pill has arrived
// over its cell, so running through three choices is one tinted object
// walking to its next slot rather than three cells blinking. Sliding is
// the settings-app gesture this skin is modelled on. One control for both
// of its uses (the analysis metric, an all-pass's order) - a second
// convention for the same job would be a second thing to learn.
void SoftSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	if (state.labels.isEmpty())
		return;

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor accent(tokens.accent);
	const QColor card(tokens.card);
	const QColor tint(tokens.cardSelected);
	const QColor chosenEdge = mixColor(card, accent, 0.42);
	const bool asleep = !state.enabled;

	// Roomy on every side so the cells keep a visible gap between them.
	const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal inset = qBound(2.0, frame.height() / 7.0, 4.0);
	const auto pillOf = [&](double index) {
		return state.segmentRect(index).adjusted(inset, inset, -inset, -inset);
	};
	const QRectF mark = pillOf(state.selectionPosition);
	const qreal pillRadius = mark.height() / 2.0;

	// Hover on a cell that is not the choice: the resting pill (the well
	// behind the light border) rising under the pointer.
	if (!asleep && state.hoveredIndex >= 0 && state.hoveredIndex != state.selectedIndex
		&& state.hoveredIndex != state.pressedIndex)
	{
		const QRectF hoverPill = pillOf(state.hoveredIndex);
		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.setBrush(QColor(tokens.surfaceSunken));
		painter.drawRoundedRect(hoverPill, pillRadius, pillRadius);
	}

	// Pressed on another cell: half the tint, the choice on its way but not
	// yet made - the release makes it. It stays below the chosen pill on
	// purpose, so a press never competes with the current choice for the
	// eye.
	if (!asleep && state.pressedIndex >= 0 && state.pressedIndex != state.selectedIndex)
	{
		const QRectF pressPill = pillOf(state.pressedIndex);
		painter.setPen(Qt::NoPen);
		painter.setBrush(mixColor(tint, card, 0.5));
		painter.drawRoundedRect(pressPill, pillRadius, pillRadius);
	}

	// The chosen pill. Pressing the current choice deepens the tint a
	// little toward the accent; asleep it is the sleeping-slot triple.
	QColor markFill = tint;
	QPen markEdge(chosenEdge, 1);
	if (asleep)
	{
		markFill = mixColor(tint, QColor(tokens.background), 0.62);
		markEdge = QPen(QColor(tokens.border), 1, Qt::DashLine);
	}
	else if (state.pressedIndex == state.selectedIndex)
	{
		markFill = mixColor(tint, accent, 0.18);
	}
	painter.setPen(markEdge);
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
		// two cells while it walks. The ink crosses to the accent exactly
		// as the tint does.
		const double arrival = mark.width() > 0.0
			? qBound(0.0, mark.intersected(cell).width() / mark.width(), 1.0)
			: 0.0;
		QColor resting(tokens.mutedText);
		if (!asleep && (i == state.hoveredIndex || i == state.pressedIndex))
			resting = QColor(tokens.text);
		painter.setPen(asleep ? QColor(tokens.mutedText) : mixColor(resting, accent, arrival));
		painter.drawText(cell, Qt::AlignCenter,
			metrics.elidedText(state.labels.at(i), Qt::ElideRight,
				int(qMax(8.0, cell.width() - inset * 2.0 - 6.0))));
	}

	// Focus is the quiet halo (alpha 90, 3px) around the whole control,
	// never a hard ring.
	if (state.focused && !asleep)
	{
		const qreal frameRadius = frame.height() / 2.0;
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(frame.adjusted(1.5, 1.5, -1.5, -1.5),
			qMax(0.0, frameRadius - 1.5), qMax(0.0, frameRadius - 1.5));
	}
}

// "A handle you cannot fumble", in the accepted mockup's drawing: a thin
// pale track, a thin accent arc, a paper face standing on a base rim that
// sits a little lower (the panels' 2px step, in the round), a round dot
// in the arc's colour for the position, and the value in a well pill
// below. Still the largest knob of the five skins.
void SoftSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor card(tokens.card);
	const QColor windowBg(tokens.background);
	const QColor border(tokens.border);
	const QColor muted(tokens.mutedText);
	const QColor accent(tokens.accent);

	// Reserve a strip at the bottom for the value pill so it sits below
	// the handle instead of floating on the face. Promoted legacy dials
	// hand in an empty valueText (their value lives in a spin box), so
	// they keep the full height for the handle.
	const bool hasBadge = !state.valueText.isEmpty();
	QRectF area(rect);
	qreal badgeHeight = 0;
	if (hasBadge)
	{
		badgeHeight = qMin<qreal>(18.0, area.height() * 0.26);
		area.setBottom(area.bottom() - badgeHeight - 1.0);
	}

	// Only a 4px inset, centred square so the handle stays circular in the
	// 100x66 legacy dial slots.
	QRectF inner = area.adjusted(4, 4, -4, -4);
	const double side = qMin(inner.width(), inner.height());
	QRectF knobRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);

	const int spanDegrees = 270;
	const int startDegrees = 135;
	const double ratio = qBound(0.0, state.ratio, 1.0);
	const double endDegrees = startDegrees + spanDegrees * ratio;
	const double centerDegrees = startDegrees + spanDegrees / 2.0;

	// Thin arcs: the track is the accent almost dissolved into the paper,
	// the value arc the accent nearly whole. One colour either side of the
	// detent - boost and cut are directions, not hues.
	const double arcWidth = qMax(4.0, side * 0.075);
	QRectF arcRect = knobRect.adjusted(arcWidth / 2.0, arcWidth / 2.0, -arcWidth / 2.0, -arcWidth / 2.0);
	const QColor trackColor = state.enabled ? mixColor(accent, card, 0.83) : withAlpha(border, 110);
	const QColor valueColor = state.enabled ? mixColor(accent, card, 0.22) : withAlpha(muted, 120);

	// Keyboard focus: a quiet halo around the whole handle, not a hard ring.
	if (state.focused && state.enabled)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 90), 3));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(knobRect.adjusted(-2, -2, 2, 2));
	}

	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(trackColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
	painter.drawArc(arcRect, -startDegrees * 16, -spanDegrees * 16);

	// The value arc grows from the start on a unipolar knob and from the
	// 12 o'clock detent either way on a gain knob.
	if (state.enabled)
	{
		painter.setPen(QPen(valueColor, arcWidth, Qt::SolidLine, Qt::RoundCap));
		if (state.bipolar)
			painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-(endDegrees - centerDegrees) * 16.0));
		else
			painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees * ratio * 16.0));
	}

	// The 0 dB detent of a gain knob: a short muted tick straddling the
	// track's outer edge at 12 o'clock (the mockup's mark), so the neutral
	// point stays marked however far the knob is turned. Only bipolar
	// knobs carry it - one more way the two knob kinds differ at a glance.
	if (state.bipolar)
	{
		const QPointF arcCenter = arcRect.center();
		const double outer = arcRect.width() / 2.0 + arcWidth / 2.0;
		painter.setPen(QPen(withAlpha(muted, state.enabled ? 220 : 110), 1.7, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(arcCenter.x(), arcCenter.y() - outer + arcWidth * 0.4),
			QPointF(arcCenter.x(), arcCenter.y() - outer - 2.0));
	}

	// The body. The base rim (one step toward the border) sits lower than
	// the face by the panels' step; a mid disc toward the well softens the
	// join; the paper face on top, anchored to the rim's upper edge so the
	// rim shows below it, with the light 1px border. Hover lifts the face
	// exactly one value step; no real shadow effects.
	const double faceInset = arcWidth + 2.5;
	QRectF faceRect = knobRect.adjusted(faceInset, faceInset, -faceInset, -faceInset);
	const double drop = qMax(1.5, side * 0.06);
	painter.setPen(Qt::NoPen);
	painter.setBrush(mixColor(card, border, 0.73));
	painter.drawEllipse(faceRect.translated(0.0, drop));
	painter.setBrush(mixColor(card, QColor(tokens.surfaceSunken), 0.65));
	painter.drawEllipse(faceRect);

	QColor faceColor = card;
	if (!state.enabled)
		faceColor = mixColor(card, windowBg, 0.5);
	else if (state.hovered || state.dragging)
		faceColor = QColor(tokens.cardHover);
	const QRectF top = faceRect.adjusted(drop / 2.0, 0.0, -drop / 2.0, -drop);
	painter.setPen(QPen(mixColor(card, border, 0.5), 1));
	painter.setBrush(faceColor);
	painter.drawEllipse(top);

	// Rounded dot indicator in the value arc's colour; it grows slightly on
	// hover and again while dragging, the calmest possible "I am held"
	// cue, and stays large enough to read from across the row.
	double dotRadius = qMax(4.0, side * 0.06);
	if (state.dragging)
		dotRadius += 1.0;
	else if (state.hovered)
		dotRadius += 0.5;
	const double dotTrack = top.width() / 2.0 - dotRadius - 2.5;
	const double radians = qDegreesToRadians(-endDegrees);
	const QPointF dotPos(top.center().x() + qCos(radians) * dotTrack,
		top.center().y() - qSin(radians) * dotTrack);
	painter.setPen(Qt::NoPen);
	painter.setBrush(valueColor);
	painter.drawEllipse(dotPos, dotRadius, dotRadius);

	// The value in a well pill below the handle: no border, the ink
	// DemiBold - the mockup's big value pill at this slot's size.
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
		painter.setPen(Qt::NoPen);
		painter.setBrush(state.enabled ? QColor(tokens.surfaceSunken) : mixColor(card, windowBg, 0.5));
		painter.drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);
		painter.setPen(state.enabled ? QColor(tokens.text) : muted);
		painter.drawText(badgeRect, Qt::AlignCenter, state.valueText);
	}
}

// The VST3 bus contract as two friendly well pills: the caption lives
// inside the pill with its value ("In  Stereo 2"), a resting selector in
// ink like every combo on this skin - a layout is a fact you may change,
// not a switched-on thing. Focus or the open menu draws the accent edge.
// Asleep (VST2, not loaded) the pill becomes the sleeping slot: dashed
// outline, sunk to the window, muted ink. Never an alarm.
void SoftSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF pill = QRectF(state.rect).adjusted(0.5, 1.5, -0.5, -1.5);
	const qreal radius = pill.height() / 2.0;
	const QColor accent(tokens.accent);
	const QColor card(tokens.card);

	QColor ink(tokens.text);
	QColor fill(tokens.surfaceSunken);
	QPen edge(QColor(tokens.border), 1);
	if (!state.enabled)
	{
		edge.setStyle(Qt::DashLine);
		fill = QColor(tokens.background);
		ink = QColor(tokens.mutedText);
	}
	else if (state.focused || state.menuOpen)
	{
		edge = QPen(accent, 1);
		fill = card;
	}
	else if (state.pressed)
	{
		fill = mixColor(QColor(tokens.cardSelected), card, 0.5);
	}
	else if (state.hovered)
	{
		fill = card;
		edge = QPen(withAlpha(accent, 128), 1);
	}
	painter.setPen(edge);
	painter.setBrush(fill);
	painter.drawRoundedRect(pill, radius, radius);

	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(9);
	QFont valueFont(tokens.fontFamily);
	valueFont.setPixelSize(11);
	valueFont.setWeight(QFont::DemiBold);

	QRectF textRect = pill.adjusted(8.0, 0, -6.0, 0);
	painter.setFont(roleFont);
	painter.setPen(state.enabled ? QColor(tokens.mutedText) : withAlphaF(ink, 0.8));
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

	// A soft chevron in the muted ink, round caps - the pill's only sharp
	// thing is nothing.
	const QPointF caretCenter(textRect.right() - 3.0, pill.center().y() - 0.5);
	painter.setPen(QPen(withAlphaF(QColor(tokens.mutedText), state.enabled ? 0.9 : 0.6), 1.6,
		Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	QPainterPath chevron;
	chevron.moveTo(caretCenter + QPointF(-3.2, -1.2));
	chevron.lineTo(caretCenter + QPointF(0.0, 2.0));
	chevron.lineTo(caretCenter + QPointF(3.2, -1.2));
	painter.drawPath(chevron);
}

// The joint is a rounded, unhurried arrow; the verdict a soft dot - the
// accent for a settled contract, the amber ink for anything to look at -
// with a body-face caption. Never monospace on this skin.
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
	// problem lives in the reference glyph (the stroke "!" transition), and
	// a dot floating beside the pills read as a second, disorienting
	// alarm (r3 judging). The caption below explains; the pills stay calm.
	if (!hasText && state.tone == VstBusFrameState::Tone::Critical)
		return;

	QColor dot(tokens.mutedText);
	switch (state.tone)
	{
	case VstBusFrameState::Tone::Success:
		dot = QColor(tokens.accent);
		break;
	case VstBusFrameState::Tone::Warning:
	case VstBusFrameState::Tone::Critical:
		dot = QColor(tokens.warning);
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
		ink = QColor(tokens.warning);
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

// The fill cells are the quieter, many-of-them controls: resting well
// pills in ink, a dashed edge on a silent slot (the not-there grammar),
// the amber ink where the assigned channel does not resolve.
void SoftSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF pill = QRectF(state.rect).adjusted(0.5, 1.5, -0.5, -1.5);
	const qreal radius = pill.height() / 2.0;
	const QColor accent(tokens.accent);
	const QColor card(tokens.card);

	QColor ink(tokens.text);
	QColor edgeColor(tokens.border);
	if (state.missingChannel)
		edgeColor = QColor(tokens.warning);
	else if (state.enabled && (state.focused || state.menuOpen))
		edgeColor = accent;
	else if (state.enabled && state.hovered)
		edgeColor = withAlpha(accent, 128);
	QPen borderPen(edgeColor, 1);
	if (state.silent)
		borderPen.setStyle(Qt::DashLine);
	QColor fill(tokens.surfaceSunken);
	if (!state.enabled)
	{
		fill = QColor(tokens.background);
		ink = QColor(tokens.mutedText);
		borderPen.setStyle(Qt::DashLine);
	}
	else if (state.pressed || state.menuOpen)
		fill = mixColor(QColor(tokens.cardSelected), card, 0.5);
	else if (state.hovered || state.focused)
		fill = card;
	painter.setPen(borderPen);
	painter.setBrush(fill);
	painter.drawRoundedRect(pill, radius, radius);

	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(9);
	QFont valueFont(tokens.fontFamily);
	valueFont.setPixelSize(11);
	valueFont.setWeight(QFont::DemiBold);

	QRectF textRect = pill.adjusted(8.0, 0, -6.0, 0);
	painter.setFont(roleFont);
	painter.setPen(withAlphaF(QColor(tokens.mutedText), state.enabled ? 1.0 : 0.6));
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleToken);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);

	painter.setFont(valueFont);
	QColor valueInk = withAlphaF(ink, state.enabled ? (state.silent || state.defaulted ? 0.55 : 0.95) : 0.6);
	if (state.missingChannel)
		valueInk = QColor(tokens.warning);
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
	painter.fillPath(caret, withAlphaF(QColor(tokens.mutedText), state.enabled ? 0.8 : 0.4));
}

void SoftSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QColor accent(tokens.accent);

	// The tray: a faint accent wash holding the pills together as one
	// gesture of the card, not a scatter of chips.
	if (!state.cellsRect.isNull() && !state.collapsed)
	{
		const QRectF tray = QRectF(state.cellsRect).adjusted(-6.0, -3.0, 6.0, 3.0);
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlphaF(accent, dark ? 0.12 : 0.10));
		painter.drawRoundedRect(tray, tray.height() / 2.0, tray.height() / 2.0);
	}

	if (state.latchRect.isNull())
		return;
	// The fold is a little switch: a dot that fills with the accent while
	// the tray is out.
	const QRectF latch(state.latchRect);
	const QColor ink(tokens.text);
	const QPointF dotCenter(latch.left() + 7.0, latch.center().y() + 0.5);
	painter.setPen(QPen(state.latchFocused ? accent : withAlphaF(ink, 0.45), 1.2));
	painter.setBrush(state.collapsed ? QBrush(Qt::NoBrush) : QBrush(accent));
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
