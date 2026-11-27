/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FlatSkin.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterStateGuard>
#include <QWidget>
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
	// The control is the list's one cap height (FlatPaint::listCapHeight),
	// centred in whatever height its widget is given (the Hilbert direction
	// control stands in a 40 px box), so it lines up with the selects and
	// buttons beside it. Outside the filter list (the analysis dock's metric
	// switch, the subwoofer routing window) it keeps its widget's height.
	bool inList = false;
	for (const QWidget* w = dynamic_cast<const QWidget*>(painter.device()); w != nullptr && !inList;
		w = w->parentWidget())
		inList = w->objectName() == QLatin1String("FilterCardRow");
	const qreal inset = inList ? qMax(0.0, (state.rect.height() - FlatPaint::listCapHeight) / 2.0) : 0.0;
	const auto segmentRect = [&state, inset](double index) {
			return state.segmentRect(index).adjusted(0.0, inset, 0.0, -inset);
		};
	const QRectF frame = QRectF(state.rect).adjusted(0.0, inset, 0.0, -inset);
	FlatPaint::box(painter, frame, QColor(tokens.card), QColor(tokens.borderStrong), FlatPaint::controlRadius);

	// A cell under the pointer or the press that is not the choice takes the
	// neutral step a hovered button takes.
	const auto neutralCell = [&](int index, const QString& token) {
			if (!state.enabled || index < 0 || index == state.selectedIndex)
				return;
			FlatPaint::box(painter, segmentRect(index).adjusted(1.0, 1.0, -1.0, -1.0), QColor(token),
				QColor(), FlatPaint::controlRadius - 1.0, 0.0);
		};
	neutralCell(state.hoveredIndex, tokens.cardHover);
	neutralCell(state.pressedIndex, tokens.cardSelected);

	// Short dividers between the cells; the chosen cell's fill covers the two
	// that touch it.
	for (int i = 1; i < state.labels.size(); i++)
	{
		const qreal x = qFloor(segmentRect(i).left());
		painter.fillRect(QRectF(x, frame.top() + 7.0, 1.0, frame.height() - 14.0), QColor(tokens.borderStrong));
	}

	const QRectF mark = segmentRect(state.selectionPosition).adjusted(-0.5, 0.0, 0.5, 0.0)
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
		const QRectF cell = segmentRect(i);
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

// The channel-fill section's words. They are UI text, so they are
// translated; the context is the block widget the section belongs to. The
// heading reuses the rails' "Channel fill", which the catalogs already have.
QString flatFillTitle()
{
	return QCoreApplication::translate("VSTSlotFillRail", "Channel fill");
}

QString flatFillHint()
{
	return QCoreApplication::translate("VSTSlotFillBlock",
		"Choose which channel goes into and comes out of each plugin slot.");
}

QString flatFillRowCaption(bool output)
{
	return output
		? QCoreApplication::translate("VSTSlotFillBlock", "Out", "row caption: the channels the plugin's output slots write")
		: QCoreApplication::translate("VSTSlotFillBlock", "In", "row caption: the channels fed into the plugin's input slots");
}

// The "-" slot as a word: nothing goes into an input slot, an output slot
// is thrown away.
QString flatFillValueText(const QString& value, bool output)
{
	if (value != QLatin1String("-"))
		return value;
	return output
		? QCoreApplication::translate("VSTSlotFillBlock", "Discard", "an output slot whose sound is thrown away")
		: QCoreApplication::translate("VSTSlotFillBlock", "Silence", "an input slot that gets no channel");
}

QFont flatFillRoleFont(const SkinTokens& tokens)
{
	return FlatPaint::bodyFont(tokens, 11);
}

QFont flatFillTitleFont(const SkinTokens& tokens)
{
	return FlatPaint::bodyFont(tokens, 13, QFont::DemiBold);
}

QFont flatFillHintFont(const SkinTokens& tokens)
{
	return FlatPaint::bodyFont(tokens, 12);
}

// The heading's measures: the chevron in its first 15, the title, 10, the
// hint (only while the section is open).
constexpr qreal flatFillChevronSpace = 15.0;
constexpr qreal flatFillHintGap = 10.0;
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
	if (state.roleInBlock)
	{
		// In the channel-fill section the slot's name stands above the box,
		// so the box holds the channel alone, and an empty slot says what
		// happens there in a word instead of "-".
		FlatSelectLook look;
		look.enabled = state.enabled;
		look.hovered = state.hovered;
		look.held = state.pressed || state.menuOpen;
		look.focused = state.focused;
		look.dashed = state.silent;
		look.failed = state.missingChannel;
		paintFlatSelect(painter, QRectF(state.rect).adjusted(0.0, 1.0, 0.0, -1.0), QString(),
			flatFillValueText(state.valueText, state.output), look, tokens);
		return;
	}
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

VstSlotFillArrangement FlatSkin::vstSlotFillArrangement() const
{
	return VstSlotFillArrangement::JoinedBlockBelowBody;
}

VstSlotFillBlockMetrics FlatSkin::vstSlotFillBlockMetrics(bool collapsed, const SkinTokens& tokens) const
{
	VstSlotFillBlockMetrics metrics;
	metrics.cellHeight = VSTSlotFillRail::cellHeight();
	metrics.roleLineHeight = 14;
	metrics.roleGap = 1;
	metrics.groupGap = 8;
	metrics.cellGap = 6;
	const QFontMetricsF caption(flatBusRoleFont(tokens));
	metrics.captionWidth = qCeil(qMax(caption.horizontalAdvance(flatFillRowCaption(false)),
		caption.horizontalAdvance(flatFillRowCaption(true))));
	metrics.captionGap = 14;
	// The heading's chevron and the row captions line up with the body's
	// first column.
	metrics.padding = QMargins(4, 0, 12, 0);
	metrics.latchPlacement = VstSlotFillBlockMetrics::LatchPlacement::HeadingLine;
	qreal headingWidth = flatFillChevronSpace + QFontMetricsF(flatFillTitleFont(tokens)).horizontalAdvance(flatFillTitle());
	if (!collapsed)
		headingWidth += flatFillHintGap + QFontMetricsF(flatFillHintFont(tokens)).horizontalAdvance(flatFillHint());
	metrics.latchWidth = qCeil(headingWidth) + 2;
	metrics.latchHeight = 20;
	metrics.latchGap = 6;
	// Each row carries its own slot names above its boxes, as the approved
	// mock drew them; differing slot lists need no rule of their own.
	metrics.shareRoleLine = false;
	metrics.rolesAboveRows = true;
	return metrics;
}

int FlatSkin::vstSlotFillBlockColumnWidth(const QString& role, const QString& value, bool output,
	const SkinTokens& tokens) const
{
	// paintFlatSelect's box without a role: 8 before the value, 20 after it
	// for the chevron, and never narrower than a box for "LFE".
	const QFontMetricsF valueMetrics(flatBusValueFont(tokens));
	const qreal valueWidth = qMax(valueMetrics.horizontalAdvance(flatFillValueText(value, output)),
		valueMetrics.horizontalAdvance(QStringLiteral("LFE")));
	const qreal roleWidth = QFontMetricsF(flatFillRoleFont(tokens)).horizontalAdvance(role);
	return qCeil(qMax(roleWidth + 2.0, 8.0 + valueWidth + 4.0 + 20.0));
}

// The channel fill as a section of an ordinary settings page: a heading
// with the fold chevron, the title in body ink and a one-line hint in the
// quiet ink; under it the two rows, a quiet caption at the left of each and
// every slot's name above its select. No backing surface.
void FlatSkin::paintVstSlotFillBlock(QPainter& painter, const VstSlotFillBlockState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	if (!state.enabled)
		painter.setOpacity(FlatPaint::disabledOpacity);

	const QColor muted(tokens.mutedText);
	painter.setPen(muted);
	painter.setFont(flatBusRoleFont(tokens));
	for (const VstSlotFillBlockState::Caption& caption : state.captions)
		painter.drawText(caption.rect, Qt::AlignLeft | Qt::AlignVCenter, flatFillRowCaption(caption.output));
	painter.setFont(flatFillRoleFont(tokens));
	for (const VstSlotFillBlockState::Role& role : state.roles)
		painter.drawText(QRectF(role.rect).adjusted(2.0, 0, 0, 0), Qt::AlignLeft | Qt::AlignBottom, role.role);

	if (state.latchRect.isNull())
		return;
	const QRectF heading(state.latchRect);
	qreal x = heading.left();
	if (state.latchActive)
	{
		// The disclosure a folded card uses: the chevron, its ink stepping up
		// under the pointer, the emphasis colour under keyboard focus.
		const QColor ink(state.enabled && state.latchFocused ? tokens.focusRing
			: (state.latchHovered || state.latchPressed ? tokens.text : tokens.mutedText));
		FlatPaint::chevron(painter, QPointF(x + 5.0, heading.center().y() + 0.5), ink, !state.collapsed, 3.0);
		x += flatFillChevronSpace;
	}
	QFont titleFont = flatFillTitleFont(tokens);
	titleFont.setUnderline(state.latchActive && state.latchHovered);
	painter.setFont(titleFont);
	painter.setPen(QColor(tokens.text));
	const QString title = flatFillTitle();
	painter.drawText(QRectF(x, heading.top(), heading.right() - x, heading.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
	if (state.collapsed)
		return;
	x += QFontMetricsF(titleFont).horizontalAdvance(title) + flatFillHintGap;
	painter.setFont(flatFillHintFont(tokens));
	painter.setPen(muted);
	painter.drawText(QRectF(x, heading.top(), qMax<qreal>(0.0, heading.right() - x + 2.0), heading.height()),
		Qt::AlignLeft | Qt::AlignVCenter, flatFillHint());
}
