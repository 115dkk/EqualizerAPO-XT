/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "HeritageSkin.h"

#include <QPainter>
#include <QPainterStateGuard>

#include "shared/SkinPaint.h"
#include "SkinThemeData.h"

QString HeritageSkin::id() const
{
	return QStringLiteral("heritage");
}

SkinTokens HeritageSkin::tokens(bool dark) const
{
	Q_UNUSED(dark);
	// Classic light values for the custom painters that consume tokens; the
	// palette is shared with Device Selector's heritage theme.
	return SkinThemeData::heritageTokens();
}

IRoutingRenderer* HeritageSkin::routingRenderer() const
{
	return nullptr;
}

void HeritageSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	// Native toolbar: the .ui's classic icons stay in place.
	Q_UNUSED(toolBar);
	Q_UNUSED(tokens);
}

void HeritageSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
	// The dialog stays platform-native in heritage mode.
	Q_UNUSED(dialog);
	Q_UNUSED(tokens);
}

QAbstractFileIconProvider* HeritageSkin::fileIconProvider(const SkinTokens& tokens) const
{
	Q_UNUSED(tokens);
	return nullptr;
}

bool HeritageSkin::logicSiblingsIndentAsMembers() const
{
	return false;
}

// The neutral painters below were ISkin's defaults until audit #422 S3 made
// those hooks pure virtual. Every product skin answers them itself and
// heritage never builds the surfaces that call them, so none of this runs
// today; it is kept unchanged so heritage would draw what the defaults drew
// if such a surface ever appeared under it.

QString HeritageSkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	const QString borderColor = info.focused ? tokens.focusRing : (info.selected ? tokens.accent : tokens.border);
	const QString backgroundColor = info.selected ? tokens.cardSelected : tokens.card;
	// Signal Matrix uses a coloured rail on the left edge to suggest routing.
	// All other skins keep a uniform 1px border.
	return tokens.cardRailWidth > 0
		? QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-left: %3px solid %4; border-radius: %5px; }")
		.arg(backgroundColor, borderColor)
		.arg(tokens.cardRailWidth)
		.arg(tokens.accent)
		.arg(tokens.borderRadius)
		: QStringLiteral("QFrame#FilterCardRow { background: %1; border: 1px solid %2; border-radius: %3px; }")
		.arg(backgroundColor, borderColor)
		.arg(tokens.borderRadius);
}

QString HeritageSkin::cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	return QStringLiteral("QWidget#FilterCardHeader { background: %1; border-top-left-radius: %2px; border-top-right-radius: %2px; }")
		.arg(info.selected ? tokens.surfaceRaised : tokens.cardHover)
		.arg(tokens.borderRadius);
}

void HeritageSkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	// Neutral default: a token-driven ghost row. A dashed 1px outline says
	// "slot, not card"; hover lifts the fill one step and inks the caption
	// with the accent so the affordance reads without a permanent icon.
	painter.setRenderHint(QPainter::Antialiasing);
	QRectF frame = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);

	if (state.hovered || state.pressed)
	{
		QColor fill(tokens.card);
		painter.setPen(Qt::NoPen);
		painter.setBrush(fill);
		painter.drawRoundedRect(frame, tokens.borderRadius, tokens.borderRadius);
	}

	QPen outline(QColor(state.hovered || state.focused ? tokens.accent : tokens.border), 1, Qt::DashLine);
	painter.setPen(outline);
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(frame, tokens.borderRadius, tokens.borderRadius);

	painter.setPen(QColor(state.hovered || state.pressed ? tokens.accent : tokens.mutedText));
	QFont font(tokens.fontFamily);
	font.setPointSizeF(10.5);
	font.setWeight(QFont::DemiBold);
	painter.setFont(font);
	painter.drawText(rect, Qt::AlignCenter, QStringLiteral("+  ") + state.label);
}

void HeritageSkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	// Neutral default: an accent hairline across the boundary with a small
	// "+" disc at the left edge. The hosting widget only shows itself while
	// hovered, so at rest nothing is painted anywhere.
	if (!state.hovered && !state.pressed)
		return;

	painter.setRenderHint(QPainter::Antialiasing);
	const QColor accent(tokens.accent);
	const int centerY = rect.center().y();
	const int discRadius = qMin(8, rect.height() / 2);
	const int discCenterX = rect.left() + discRadius + 4;

	painter.setPen(QPen(accent, 2));
	painter.drawLine(discCenterX + discRadius + 4, centerY, rect.right() - 4, centerY);

	painter.setPen(Qt::NoPen);
	painter.setBrush(accent);
	painter.drawEllipse(QPoint(discCenterX, centerY), discRadius, discRadius);

	painter.setPen(QColor(tokens.background));
	QFont font(tokens.fontFamily);
	font.setPixelSize(discRadius * 2 - 3);
	font.setWeight(QFont::Bold);
	painter.setFont(font);
	painter.drawText(QRect(discCenterX - discRadius, centerY - discRadius, discRadius * 2, discRadius * 2),
		Qt::AlignCenter, QStringLiteral("+"));
}

void HeritageSkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
{
	// Neutral default: a quiet token-driven instrument. Crisp 1px grid (no
	// antialiasing on straight lines), an accent response curve with a soft
	// fill down to 0 dB, plain disc handles. Skins override this wholesale.
	const QColor ground(tokens.graph);
	const QColor border(tokens.border);
	const QColor gridMinor(tokens.graphGridMinor);
	const QColor gridMajor(tokens.graphGridMajor);
	const QColor muted(tokens.mutedText);
	const QColor accent(tokens.accent);
	const QColor card(tokens.card);

	if (!state.enabled)
		painter.setOpacity(0.45);

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.fillRect(state.rect, ground);

	QFont labelFont(tokens.monoFontFamily);
	labelFont.setPointSizeF(8.5);
	painter.setFont(labelFont);

	for (const GraphicEQPlotState::GridLine& line : state.vertical)
	{
		const int x = int(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(x, int(state.plotRect.top()), x, int(state.plotRect.bottom()));
		if (!line.label.isEmpty())
		{
			painter.setPen(muted);
			painter.drawText(QRect(x - 24, int(state.plotRect.bottom()) + 2, 48, state.rect.bottom() - int(state.plotRect.bottom()) - 2),
				Qt::AlignHCenter | Qt::AlignTop, line.label);
		}
	}
	for (const GraphicEQPlotState::GridLine& line : state.horizontal)
	{
		const int y = int(line.pos);
		painter.setPen(QPen(line.major ? gridMajor : gridMinor, 1));
		painter.drawLine(int(state.plotRect.left()), y, int(state.plotRect.right()), y);
		if (!line.label.isEmpty())
		{
			painter.setPen(muted);
			painter.drawText(QRect(state.rect.left(), y - 8, int(state.plotRect.left()) - state.rect.left() - 4, 16),
				Qt::AlignRight | Qt::AlignVCenter, line.label);
		}
	}

	// The 0 dB baseline reads a step above the ordinary grid.
	if (state.zeroY >= state.plotRect.top() && state.zeroY <= state.plotRect.bottom())
	{
		QColor zero(muted);
		zero.setAlpha(170);
		painter.setPen(QPen(zero, 1));
		painter.drawLine(int(state.plotRect.left()), int(state.zeroY), int(state.plotRect.right()), int(state.zeroY));
	}

	painter.setRenderHint(QPainter::Antialiasing, true);

	if (state.curve.size() >= 2)
	{
		QPolygonF fill = state.curve;
		const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
		fill.append(QPointF(state.curve.last().x(), base));
		fill.prepend(QPointF(state.curve.first().x(), base));
		QColor fillColor(accent);
		fillColor.setAlpha(30);
		painter.setPen(Qt::NoPen);
		painter.setBrush(fillColor);
		painter.drawPolygon(fill);

		painter.setPen(QPen(accent, 1.6));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(state.curve);
	}

	// Band-locked layouts read as levels on fixed bands: a stem under each
	// handle keeps the classic graphic-EQ silhouette.
	if (state.bandLocked)
	{
		QColor stem(accent);
		stem.setAlpha(90);
		painter.setPen(QPen(stem, 2));
		const double base = qBound(state.plotRect.top(), state.zeroY, state.plotRect.bottom());
		for (const QPointF& node : state.nodePositions)
			painter.drawLine(QPointF(node.x(), base), node);
	}

	for (int i = 0; i < state.nodePositions.size(); i++)
	{
		const QPointF& center = state.nodePositions.at(i);
		const bool selected = state.selectedNodes.contains(i);
		const bool hovered = state.hoveredNode == i;
		const double radius = hovered || selected ? 5.0 : 4.0;
		painter.setPen(QPen(accent, selected ? 2.0 : 1.4));
		painter.setBrush(selected ? accent : card);
		painter.drawEllipse(center, radius, radius);
	}

	if (state.cursorValid && !state.cursorText.isEmpty())
	{
		painter.setPen(muted);
		painter.setFont(labelFont);
		painter.drawText(QRectF(state.plotRect.adjusted(0, 2, -6, 0)), Qt::AlignRight | Qt::AlignTop, state.cursorText);
	}

	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QPen(state.focused ? QColor(tokens.focusRing) : border, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(state.rect.adjusted(0, 0, -1, -1));
}

void HeritageSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	// Neutral default: a quiet token-coloured cell - muted role caption, the
	// layout token in data ink, the channel count and a caret. Deliberately
	// unspectacular; each shipped skin answers with its own instrument.
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF cell = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMin(cell.height() / 4.0, qreal(qMax(2, tokens.borderRadius / 2)));
	QColor border(state.focused || state.menuOpen ? tokens.focusRing : tokens.border);
	if (!state.enabled)
		border = withAlpha(QColor(tokens.border), 130);
	else if (state.hovered && !state.focused && !state.menuOpen)
		border = QColor(tokens.mutedText);
	painter.setPen(QPen(border, 1));
	painter.setBrush(QColor(state.pressed || state.menuOpen ? tokens.cardHover : tokens.surface));
	painter.drawRoundedRect(cell, radius, radius);

	QColor roleInk(tokens.mutedText);
	QColor valueInk(tokens.text);
	if (!state.enabled)
	{
		roleInk = withAlpha(roleInk, 130);
		valueInk = withAlpha(QColor(tokens.mutedText), 170);
	}

	const int pad = 8;
	QRectF textRect = cell.adjusted(pad, 0, -pad, 0);

	QFont roleFont = painter.font();
	roleFont.setPixelSize(10);
	painter.setFont(roleFont);
	painter.setPen(roleInk);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleText);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleText);

	// Caret: a small down triangle at the right edge, the shared dropdown cue.
	const qreal caretWidth = 7;
	const QPointF caretCenter(textRect.right() - caretWidth / 2.0, cell.center().y() + 0.5);
	QPainterPath caret;
	caret.moveTo(caretCenter + QPointF(-caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(0.0, caretWidth / 2.0));
	caret.closeSubpath();
	painter.fillPath(caret, roleInk);

	QFont valueFont = painter.font();
	valueFont.setPixelSize(13);
	painter.setFont(valueFont);
	painter.setPen(valueInk);
	QString value = state.layoutText;
	if (state.channelCount > 0)
		value += QStringLiteral(" %1").arg(state.channelCount);
	textRect.setLeft(textRect.left() + roleWidth + 6);
	textRect.setRight(textRect.right() - caretWidth - 4);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, value);
}

void HeritageSkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
{
	// Neutral default: no ground panel, a painted arrow through the joint and
	// the verdict text behind a small tone lamp.
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	QColor muted(tokens.mutedText);
	if (!state.enabled)
		muted = withAlpha(muted, 150);

	// The direction mark: shaft plus chevron, drawn instead of a text arrow so
	// no font coverage decides whether the signal flow is visible.
	const qreal midY = state.jointRect.center().y() + 0.5;
	const qreal margin = qMax(2.0, state.jointRect.width() / 5.0);
	const QPointF tail(state.jointRect.left() + margin, midY);
	const QPointF head(state.jointRect.right() - margin, midY);
	painter.setPen(QPen(muted, 1.2, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(tail, head);
	const qreal barb = qMin(4.0, state.jointRect.width() / 4.0);
	painter.drawLine(head, head + QPointF(-barb, -barb));
	painter.drawLine(head, head + QPointF(-barb, barb));

	// A tone without words is a lamp-only verdict (an accepted explicit
	// contract - the selectors already print the pair).
	if (!state.showVerdict)
		return;

	QColor lamp(tokens.*state.toneToken);
	if (!state.enabled)
		lamp = withAlpha(lamp, 150);

	const qreal lampRadius = 3;
	painter.setPen(Qt::NoPen);
	painter.setBrush(lamp);
	painter.drawEllipse(QPointF(state.verdictRect.left() + lampRadius,
		state.verdictRect.center().y() + 0.5), lampRadius, lampRadius);
	if (!state.hasText)
		return;

	QFont verdictFont = painter.font();
	verdictFont.setPixelSize(11);
	painter.setFont(verdictFont);
	const QColor ink = state.tone == VstBusFrameState::Tone::Critical ? lamp : muted;
	painter.setPen(ink);
	QRectF textRect(state.verdictRect);
	textRect.setLeft(textRect.left() + lampRadius * 2.0 + 5);
	if (state.pairVerdict)
	{
		// The pair joins over the same painted mark as the joint, so the
		// readout repeats the strip's direction grammar instead of a glyph.
		const QFontMetricsF metrics(verdictFont);
		const qreal inWidth = metrics.horizontalAdvance(state.verdictInputText);
		const qreal markWidth = 14;
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.verdictInputText);
		const qreal markLeft = textRect.left() + inWidth + 3;
		const qreal midY = textRect.center().y() + 0.5;
		painter.setPen(QPen(ink, 1.1, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(markLeft, midY), QPointF(markLeft + markWidth - 6, midY));
		const QPointF head(markLeft + markWidth - 6, midY);
		painter.drawLine(head, head + QPointF(-3.0, -3.0));
		painter.drawLine(head, head + QPointF(-3.0, 3.0));
		painter.setPen(ink);
		QRectF outRect(textRect);
		outRect.setLeft(markLeft + markWidth);
		painter.drawText(outRect, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(verdictFont).elidedText(state.verdictOutputText, Qt::ElideRight, qRound(outRect.width())));
	}
	else
	{
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetrics(verdictFont).elidedText(state.verdictText, Qt::ElideRight, qRound(textRect.width())));
	}
}

void HeritageSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	// Neutral default, deliberately unlike the bus selector: no channel-count
	// suffix and a flatter cell, so a channel pick never reads as a layout
	// pick. vstSlotFillCellSize measures these font sizes and paddings.
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF cell = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMin(cell.height() / 5.0, qreal(qMax(2, tokens.borderRadius / 3)));
	QColor border(state.focused || state.menuOpen ? tokens.focusRing : tokens.border);
	if (state.missingChannel)
		border = QColor(tokens.danger);
	else if (!state.enabled)
		border = withAlpha(QColor(tokens.border), 130);
	else if (state.hovered && !state.focused && !state.menuOpen)
		border = QColor(tokens.mutedText);
	painter.setPen(QPen(border, 1));
	painter.setBrush(QColor(state.pressed || state.menuOpen ? tokens.cardHover : tokens.surfaceSunken));
	painter.drawRoundedRect(cell, radius, radius);

	QColor roleInk(tokens.mutedText);
	QColor valueInk(tokens.text);
	if (state.silent || state.defaulted)
		valueInk = QColor(tokens.mutedText);
	if (state.missingChannel)
		valueInk = QColor(tokens.danger);
	if (!state.enabled)
	{
		roleInk = withAlpha(roleInk, 130);
		valueInk = withAlpha(valueInk, 150);
	}

	const qreal pad = 6;
	QRectF textRect = cell.adjusted(pad, 0, -pad, 0);

	QFont roleFont = painter.font();
	roleFont.setPixelSize(10);
	painter.setFont(roleFont);
	painter.setPen(roleInk);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleToken);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);

	const qreal caretWidth = 6;
	const QPointF caretCenter(textRect.right() - caretWidth / 2.0, cell.center().y() + 0.5);
	QPainterPath caret;
	caret.moveTo(caretCenter + QPointF(-caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(0.0, caretWidth / 2.0));
	caret.closeSubpath();
	painter.fillPath(caret, roleInk);

	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(12);
	painter.setFont(valueFont);
	painter.setPen(valueInk);
	textRect.setLeft(textRect.left() + roleWidth + 5);
	textRect.setRight(textRect.right() - caretWidth - 4);
	painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, state.valueText);
}

void HeritageSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	// Neutral default: a quiet sunken tray under the cells, and the fold
	// latch as a small labelled toggle - a state dot plus the FILL token -
	// so the whole-feature fold never reads as one more channel dropdown.
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	if (!state.cellsRect.isNull() && !state.collapsed)
	{
		const QRectF tray = QRectF(state.cellsRect).adjusted(-4, -3,
			4, 3);
		painter.setPen(QPen(withAlpha(QColor(tokens.border), 120), 1));
		painter.setBrush(withAlpha(QColor(tokens.surfaceSunken), 90));
		painter.drawRoundedRect(tray, 4, 4);
	}

	if (state.latchRect.isNull())
		return;

	const QRectF latch = QRectF(state.latchRect).adjusted(0.5, 0.5, -0.5, -0.5);
	QColor latchBorder(state.latchFocused ? tokens.focusRing : tokens.border);
	if (state.latchHovered && !state.latchFocused)
		latchBorder = QColor(tokens.mutedText);
	painter.setPen(QPen(latchBorder, 1));
	painter.setBrush(QColor(state.latchPressed ? tokens.cardHover : tokens.surface));
	painter.drawRoundedRect(latch, latch.height() / 2.0, latch.height() / 2.0);

	const qreal dotRadius = 3;
	const QColor dot = state.collapsed ? QColor(tokens.mutedText) : QColor(tokens.accent);
	painter.setPen(Qt::NoPen);
	painter.setBrush(state.enabled ? dot : withAlpha(dot, 140));
	painter.drawEllipse(QPointF(latch.left() + 9, latch.center().y() + 0.5),
		dotRadius, dotRadius);

	QFont latchFont = painter.font();
	latchFont.setPixelSize(10);
	painter.setFont(latchFont);
	painter.setPen(QColor(state.collapsed ? tokens.mutedText : tokens.text));
	QRectF labelRect = latch;
	labelRect.setLeft(latch.left() + 15);
	painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FILL"));
}

ISkin* heritageSkin()
{
	static HeritageSkin instance;
	return &instance;
}
