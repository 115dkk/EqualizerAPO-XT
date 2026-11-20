/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FlatSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/widgets/cards/VSTSlotFillRail.h"
#include "FlatPaint.h"

// One of a few choices, as the segmented control every desktop toolkit has:
// a bordered row of cells, the chosen cell filled with the emphasis colour.
// The fill is drawn at selectionPosition, so a change of choice slides
// instead of blinking, and each label's ink crosses over in step with it.
void FlatSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	if (state.labels.isEmpty())
		return;

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	if (!state.enabled)
		painter.setOpacity(FlatPaint::disabledOpacity);

	const QColor accent(tokens.accent);
	const QColor onAccent(tokens.onAccent);
	const QRectF frame(state.rect);
	FlatPaint::box(painter, frame, QColor(tokens.card), QColor(tokens.borderStrong), FlatPaint::controlRadius);

	// A cell under the pointer or the press that is not the choice takes the
	// neutral step a hovered button takes.
	const auto neutralCell = [&](int index, const QString& token) {
			if (!state.enabled || index < 0 || index == state.selectedIndex)
				return;
			FlatPaint::box(painter, state.segmentRect(index).adjusted(1.0, 1.0, -1.0, -1.0), QColor(token),
				QColor(), FlatPaint::controlRadius - 1.0, 0.0);
		};
	neutralCell(state.hoveredIndex, tokens.cardHover);
	neutralCell(state.pressedIndex, tokens.cardSelected);

	// Short dividers between the cells; the chosen cell's fill covers the two
	// that touch it.
	for (int i = 1; i < state.labels.size(); i++)
	{
		const qreal x = qFloor(state.segmentRect(i).left());
		painter.fillRect(QRectF(x, frame.top() + 7.0, 1.0, frame.height() - 14.0), QColor(tokens.borderStrong));
	}

	const QRectF mark = state.segmentRect(state.selectionPosition).adjusted(-0.5, 0.0, 0.5, 0.0)
		.intersected(frame);
	// Keyboard focus is a line on the chosen cell's own outline (shared
	// article 8). The cell is the emphasis colour itself, so the line is that
	// colour taken toward the ink that sits on it.
	const bool focusLine = state.focused && state.enabled;
	FlatPaint::box(painter, mark, accent, focusLine ? mixColor(accent, onAccent, 0.45) : QColor(),
		FlatPaint::controlRadius, focusLine ? 2.0 : 0.0);

	const QFont restFont = FlatPaint::bodyFont(tokens, 13);
	const QFont chosenFont = FlatPaint::bodyFont(tokens, 13, QFont::DemiBold);
	for (int i = 0; i < state.labels.size(); i++)
	{
		const QRectF cell = state.segmentRect(i);
		// How much of the travelling fill has arrived over this cell.
		const double arrival = mark.width() > 0.0
			? qBound(0.0, mark.intersected(cell).width() / mark.width(), 1.0)
			: 0.0;
		const QFont& font = arrival > 0.5 ? chosenFont : restFont;
		painter.setFont(font);
		painter.setPen(mixColor(QColor(tokens.text), onAccent, arrival));
		painter.drawText(cell, Qt::AlignCenter,
			QFontMetricsF(font).elidedText(state.labels.at(i), Qt::ElideRight, int(qMax(8.0, cell.width() - 6.0))));
	}
}

// A plain dial: a thin track, the value drawn on it in the emphasis colour,
// a flat face with one pointer line. A two-sided value (gain) grows its arc
// from a tick at 12 o'clock, so it reads as two-sided while it rests at 0.
void FlatSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	if (!state.enabled)
		painter.setOpacity(FlatPaint::disabledOpacity);

	// The value is not repeated under the handle: every knob sits beside the
	// box that holds its value (state.valueText is the same number), and a
	// state is said once. The handle takes the whole rect.
	const QRectF area(rect);

	const qreal trackWidth = 3.0;
	// The 4px inset leaves room for the half of the 12 o'clock tick that
	// stands proud of the track.
	const QRectF inner = area.adjusted(4.0, 4.0, -4.0, -4.0);
	const qreal side = qMin(inner.width(), inner.height());
	const QPointF center = inner.center();
	const qreal trackRadius = side / 2.0 - trackWidth / 2.0;
	const QRectF arcRect(center.x() - trackRadius, center.y() - trackRadius, trackRadius * 2.0, trackRadius * 2.0);

	const int spanDegrees = 270;
	const int startDegrees = 135;
	const double ratio = qBound(0.0, state.ratio, 1.0);
	const double endDegrees = startDegrees + spanDegrees * ratio;
	const double centerDegrees = startDegrees + spanDegrees / 2.0;

	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QColor(tokens.borderStrong), trackWidth, Qt::SolidLine, Qt::RoundCap));
	painter.drawArc(arcRect, -startDegrees * 16, -spanDegrees * 16);

	painter.setPen(QPen(QColor(state.enabled ? tokens.accent : tokens.mutedText), trackWidth, Qt::SolidLine, Qt::RoundCap));
	if (state.bipolar)
	{
		if (qAbs(ratio - 0.5) > 0.004)
			painter.drawArc(arcRect, qRound(-centerDegrees * 16.0), qRound(-(endDegrees - centerDegrees) * 16.0));
		painter.setPen(QPen(QColor(tokens.text), 1.5, Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(QPointF(center.x(), center.y() - trackRadius - 3.5),
			QPointF(center.x(), center.y() - trackRadius + 3.5));
	}
	else if (ratio > 0.004)
		painter.drawArc(arcRect, -startDegrees * 16, qRound(-spanDegrees * ratio * 16.0));

	// The face. Hover and drag take the neutral step; keyboard focus is the
	// face's own edge in the emphasis colour, one pixel heavier (article 8).
	const qreal faceRadius = qMax<qreal>(4.0, trackRadius - trackWidth / 2.0 - 4.5);
	const bool focused = state.focused && state.enabled;
	const qreal edgeWidth = focused ? 2.0 : 1.0;
	painter.setPen(QPen(QColor(focused ? tokens.focusRing : tokens.controlEdge), edgeWidth));
	painter.setBrush(QColor(state.enabled && (state.hovered || state.dragging) ? tokens.cardHover : tokens.card));
	painter.drawEllipse(center, faceRadius - edgeWidth / 2.0, faceRadius - edgeWidth / 2.0);

	const QPointF tip = skinArcPoint(center, faceRadius * 0.74, -endDegrees);
	const QPointF tail = skinArcPoint(center, faceRadius * 0.36, -endDegrees);
	painter.setPen(QPen(QColor(tokens.text), state.dragging ? 2.5 : 2.0, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(tail, tip);
}

namespace
{
QFont flatBusRoleFont(const SkinTokens& tokens, bool compact = false)
{
	return FlatPaint::bodyFont(tokens, compact ? 11 : 12);
}

QFont flatBusValueFont(const SkinTokens& tokens)
{
	return FlatPaint::bodyFont(tokens, 13);
}

// The select every VST part is drawn as: a bordered box, the name of what
// is chosen (role) in the quiet ink, the choice in full ink, a chevron.
struct FlatSelectLook
{
	bool enabled = true;
	bool hovered = false;
	bool held = false;
	bool focused = false;
	bool dashed = false;
	bool failed = false;
	// The bus selector's width is fixed by VSTBusStrip for every skin
	// (selectorWidth: 6 either side, 4 after the role, 9 for the caret), so
	// there the box keeps to that budget instead of its own roomier one.
	bool compact = false;
};

void paintFlatSelect(QPainter& painter, const QRectF& rect, const QString& role, const QString& value,
	const FlatSelectLook& look, const SkinTokens& tokens)
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	if (!look.enabled)
		painter.setOpacity(FlatPaint::disabledOpacity);

	QColor edge(tokens.controlEdge);
	qreal edgeWidth = 1.0;
	if (look.failed)
		edge = QColor(tokens.danger);
	if (look.enabled && (look.focused || look.held))
	{
		// A failed box keeps its colour under focus: the state outranks the
		// cursor, and the heavier line still says where the focus is.
		if (!look.failed)
			edge = QColor(tokens.focusRing);
		edgeWidth = 2.0;
	}
	QColor fill(tokens.card);
	if (look.enabled && look.held)
		fill = QColor(tokens.cardSelected);
	else if (look.enabled && look.hovered)
		fill = QColor(tokens.cardHover);
	FlatPaint::box(painter, rect, fill, edge, look.compact ? 5.0 : FlatPaint::controlRadius, edgeWidth,
		look.dashed ? Qt::DashLine : Qt::SolidLine);

	const QRectF textRect = look.compact ? rect.adjusted(6.0, 0.0, -11.0, 0.0) : rect.adjusted(8.0, 0.0, -20.0, 0.0);
	const QFont roleFont = flatBusRoleFont(tokens, look.compact);
	const QFont valueFont = flatBusValueFont(tokens);
	qreal valueLeft = textRect.left();
	if (!role.isEmpty())
	{
		painter.setFont(roleFont);
		painter.setPen(QColor(tokens.mutedText));
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, role);
		valueLeft += QFontMetricsF(roleFont).horizontalAdvance(role) + (look.compact ? 4.0 : 6.0);
	}
	painter.setFont(valueFont);
	painter.setPen(QColor(look.failed ? tokens.danger : (look.dashed ? tokens.mutedText : tokens.text)));
	const QRectF valueRect(valueLeft, textRect.top(), qMax<qreal>(0.0, textRect.right() - valueLeft), textRect.height());
	painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetricsF(valueFont).elidedText(value, Qt::ElideRight, valueRect.width()));

	FlatPaint::chevron(painter, QPointF(rect.right() - (look.compact ? 7.0 : 11.0), rect.center().y()),
		QColor(tokens.mutedText), true, look.compact ? 2.5 : 3.0);
}
}

// The VST3 bus contract as two ordinary selects, "In Stereo 2" and
// "Out 5.1 6". A plugin that has no layouts to choose draws them faded.
void FlatSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	QString value = state.layoutText;
	if (state.channelCount > 0)
		value += QStringLiteral(" %1").arg(state.channelCount);
	FlatSelectLook look;
	look.enabled = state.enabled;
	look.hovered = state.hovered;
	look.held = state.pressed || state.menuOpen;
	look.focused = state.focused;
	look.compact = true;
	paintFlatSelect(painter, QRectF(state.rect), state.roleText, value, look, tokens);
}

// Between the two selects, an arrow. The verdict is a state said once: one
// lamp in the state colour and, where there is something to add, a caption
// in the body ink.
void FlatSkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	if (!state.enabled)
		painter.setOpacity(FlatPaint::disabledOpacity);

	const QColor arrowInk(tokens.mutedText);
	const qreal midY = state.jointRect.center().y() + 0.5;
	painter.setPen(QPen(arrowInk, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.drawLine(QPointF(state.jointRect.left() + 3.5, midY),
		QPointF(state.jointRect.right() - 3.5, midY));
	const QPointF head(state.jointRect.right() - 3.5, midY);
	painter.drawLine(head, head + QPointF(-3.5, -3.5));
	painter.drawLine(head, head + QPointF(-3.5, 3.5));

	if (!state.showVerdict)
		return;

	// A wordless failure draws nothing here: the card's own state line (the
	// lamp beside the plugin's name) has said it, and a second red dot beside
	// the selects would say it twice (shared article 1).
	if (!state.hasText && state.tone == VstBusFrameState::Tone::Critical)
		return;

	const QPointF lampCenter(state.verdictRect.left() + 4.0, state.verdictRect.center().y() + 0.5);
	FlatPaint::lamp(painter, lampCenter, QColor(tokens.*state.toneToken),
		state.tone == VstBusFrameState::Tone::Neutral);

	if (!state.hasText)
		return;

	const QFont captionFont = FlatPaint::bodyFont(tokens, 13);
	const QFontMetricsF metrics(captionFont);
	painter.setFont(captionFont);
	const QColor ink(state.tone == VstBusFrameState::Tone::Neutral ? tokens.mutedText : tokens.text);
	painter.setPen(ink);
	QRectF textRect(state.verdictRect);
	textRect.setLeft(lampCenter.x() + 10.0);
	if (state.pairVerdict)
	{
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.verdictInputText);
		const qreal markLeft = textRect.left() + metrics.horizontalAdvance(state.verdictInputText) + 5.0;
		const qreal markY = textRect.center().y() + 0.5;
		painter.setPen(QPen(QColor(tokens.mutedText), 1.3, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(markLeft, markY), QPointF(markLeft + 8.0, markY));
		painter.drawLine(QPointF(markLeft + 8.0, markY), QPointF(markLeft + 5.5, markY - 2.5));
		painter.drawLine(QPointF(markLeft + 8.0, markY), QPointF(markLeft + 5.5, markY + 2.5));
		painter.setPen(ink);
		QRectF outRect(textRect);
		outRect.setLeft(markLeft + 13.0);
		painter.drawText(outRect, Qt::AlignLeft | Qt::AlignVCenter,
			metrics.elidedText(state.verdictOutputText, Qt::ElideRight, outRect.width()));
	}
	else
	{
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
			metrics.elidedText(state.verdictText, Qt::ElideRight, textRect.width()));
	}
}

QSize FlatSkin::vstSlotFillCellSize(const QString& role, const QString& value, const SkinTokens& tokens) const
{
	// paintFlatSelect's box: 8 before the role, 6 between role and value, and
	// 20 after the value for the chevron.
	const qreal roleWidth = QFontMetricsF(flatBusRoleFont(tokens)).horizontalAdvance(role);
	const qreal valueWidth = QFontMetricsF(flatBusValueFont(tokens)).horizontalAdvance(value);
	return QSize(qCeil(8.0 + roleWidth + 6.0 + valueWidth + 4.0 + 20.0), VSTSlotFillRail::cellHeight());
}

// One slot of the plugin: the slot's name and the channel put into it, as a
// small select. A slot left empty wears the dashed line, the skin's one mark
// for "nothing here"; a channel that is not there takes the failure colour.
void FlatSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	FlatSelectLook look;
	look.enabled = state.enabled;
	look.hovered = state.hovered;
	look.held = state.pressed || state.menuOpen;
	look.focused = state.focused;
	look.dashed = state.silent;
	look.failed = state.missingChannel;
	paintFlatSelect(painter, QRectF(state.rect).adjusted(0.0, 1.0, 0.0, -1.0), state.roleToken, state.valueText,
		look, tokens);
}

// The row of slots needs no tray: the selects stand in a line. The fold is a
// chevron and a word, the same disclosure a folded card uses.
void FlatSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	if (state.latchRect.isNull())
		return;
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	if (!state.enabled)
		painter.setOpacity(FlatPaint::disabledOpacity);

	const QRectF latch(state.latchRect);
	const QColor ink(state.latchFocused ? tokens.focusRing
		: (state.latchHovered || state.latchPressed ? tokens.text : tokens.mutedText));
	FlatPaint::chevron(painter, QPointF(latch.left() + 6.0, latch.center().y() + 0.5), ink, !state.collapsed, 3.0);
	QFont latchFont = FlatPaint::bodyFont(tokens, 12);
	latchFont.setUnderline(state.latchHovered);
	painter.setFont(latchFont);
	painter.setPen(QColor(state.latchHovered || state.latchPressed ? tokens.text : tokens.mutedText));
	painter.drawText(QRectF(latch.left() + 15.0, latch.top(), latch.width() - 15.0, latch.height()),
		Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Fill"));
}
