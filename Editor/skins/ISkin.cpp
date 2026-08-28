/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Neutral default implementations of the ISkin hooks, shared by every
	skin that does not override them.
*/

#include "ISkin.h"

#include <QAction>
#include <QFileDialog>
#include <QPainter>
#include <QPainterStateGuard>
#include <QToolBar>
#include <QToolButton>
#include <QtMath>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/widgets/FilterPickerView.h"
#include "Editor/widgets/cards/DefaultReferenceCardView.h"
#include "Editor/widgets/cards/SubwooferRoutingCardView.h"
#include "shared/SkinPaint.h"
#include "SkinThemeData.h"

namespace
{
// Point on the largest circle inscribed in rect; the shared skinArcPoint owns
// the screen-Y trig (Qt angles run counter-clockwise from 3 o'clock).
QPointF pointOnArc(const QRectF& rect, double degrees)
{
	return skinArcPoint(rect.center(), qMin(rect.width(), rect.height()) / 2.0, degrees);
}
}

SkinTokens ISkin::tokens(bool dark) const
{
	return SkinThemeData::tokens(id(), dark);
}

QString ISkin::qssResource(bool dark) const
{
	return SkinThemeData::qssResource(id(), dark);
}

void ISkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	// Draw inside a centred square so the knob stays circular even when the
	// hosting widget is not square (promoted legacy dials are 100x66).
	QRectF inner = QRectF(rect).adjusted(9, 9, -9, -9);
	double side = qMin(inner.width(), inner.height());
	QRectF knobRect(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);
	int spanDegrees = 270;
	int startDegrees = 135;
	double ratio = state.ratio;

	QPen trackPen(QColor(tokens.border), 6, Qt::SolidLine, Qt::RoundCap);
	painter.setPen(trackPen);
	painter.drawArc(knobRect, -startDegrees * 16, -spanDegrees * 16);

	QPen valuePen(QColor(tokens.accent), 6, Qt::SolidLine, Qt::RoundCap);
	painter.setPen(valuePen);
	painter.drawArc(knobRect, -startDegrees * 16, -static_cast<int>(spanDegrees * ratio * 16));

	QColor fill(tokens.card);
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(fill);
	painter.drawEllipse(knobRect.adjusted(6, 6, -6, -6));

	double endDegrees = startDegrees + spanDegrees * ratio;
	QPointF dot = pointOnArc(knobRect.adjusted(3, 3, -3, -3), -endDegrees);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(tokens.accent));
	painter.drawEllipse(dot, 4, 4);

	// Only draw centred text when an explicit value string was supplied (e.g.
	// the Preamp card). Promoted legacy dials drive a separate spin box for the
	// real value and map the dial to log-scaled steps, so painting value() here
	// would show a meaningless step count.
	if (!state.valueText.isEmpty())
	{
		painter.setPen(QColor(tokens.text));
		QFont valueFont = painter.font();
		valueFont.setBold(true);
		valueFont.setPointSizeF(qMax(7.0, valueFont.pointSizeF() - 1.0));
		painter.setFont(valueFont);
		painter.drawText(rect, Qt::AlignCenter, state.valueText);
	}
}

bool ISkin::paintChannelBadge(QPainter& painter, const QRect& rect, const QString& channel,
	bool virtualChannel, const SkinTokens& tokens) const
{
	Q_UNUSED(painter);
	Q_UNUSED(rect);
	Q_UNUSED(channel);
	Q_UNUSED(virtualChannel);
	Q_UNUSED(tokens);
	return false;
}

QString ISkin::cardFrameStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
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

QString ISkin::cardHeaderStyle(const CommandRowInfo& info, const SkinTokens& tokens) const
{
	return QStringLiteral("QWidget#FilterCardHeader { background: %1; border-top-left-radius: %2px; border-top-right-radius: %2px; }")
		.arg(info.selected ? tokens.surfaceRaised : tokens.cardHover)
		.arg(tokens.borderRadius);
}

// Outline-style skins ink the badge in the type colour, filled-style skins
// use the type colour as the pill background.
BadgeTreatment ISkin::badgeTreatment(const CommandRowInfo& info, const QString& typeColor,
	const QString& badgeToken, const SkinTokens& tokens) const
{
	Q_UNUSED(info);
	Q_UNUSED(badgeToken);
	const bool outlineBadge = tokens.badgeStyle == SkinTokens::OutlineOnly || tokens.badgeStyle == SkinTokens::WireframeBorder;
	return {
		QStringLiteral("color:%1; border-color:%2; background-color:%3;")
			.arg(outlineBadge ? typeColor : QStringLiteral("white"),
				typeColor,
				outlineBadge ? QStringLiteral("transparent") : typeColor),
		outlineBadge ? QColor(typeColor) : QColor(Qt::white)
	};
}

void ISkin::prepareCommandRow(const CommandRowInfo&, QWidget*, QWidget*, QWidget*, const SkinTokens&) const
{
	// Neutral default: rows keep their stock construction.
}

void ISkin::paintCardChrome(QPainter&, const QRect&, const CommandRowInfo&, const SkinTokens&) const
{
	// Neutral default: no painted decoration on top of the QSS chrome.
}

bool ISkin::paintScopeGutter(QPainter&, const QSize&, const CommandRowInfo&, const SkinTokens&) const
{
	// Neutral default: the shared channel-rail painting stays in charge.
	return false;
}

bool ISkin::logicSiblingsIndentAsMembers() const
{
	// Neutral default: ElseIf/Else/EndIf align with their If head.
	return false;
}

void ISkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
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

void ISkin::paintGraphicEqPlot(QPainter& painter, const GraphicEQPlotState& state, const SkinTokens& tokens) const
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

void ISkin::paintAnalysisGraph(QPainter& painter, const AnalysisGraphState& state, const SkinTokens& tokens) const
{
	// Neutral default: a rounded token ground, the token grid, an accent
	// zero line and an accent response trace over a translucent fill. Also
	// the heritage look.
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor accent(tokens.accent);
	const QColor muted(tokens.mutedText);
	const bool dark = tokens.dark;

	QRectF bgRect = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMax(0, tokens.borderRadius - 2);
	QPainterPath bgPath;
	bgPath.addRoundedRect(bgRect, radius, radius);
	painter.fillPath(bgPath, QColor(tokens.graph));
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.drawPath(bgPath);
	painter.setClipPath(bgPath);

	QPen gridPen(QColor(tokens.graphGridMinor.isEmpty() ? tokens.border : tokens.graphGridMinor), 1);
	gridPen.setCosmetic(true);
	painter.setPen(gridPen);
	for (const AnalysisGraphState::GridLine& line : state.vertical)
		painter.drawLine(QPointF(line.pos, state.plotRect.top()), QPointF(line.pos, state.plotRect.bottom()));
	for (const AnalysisGraphState::GridLine& line : state.horizontal)
		painter.drawLine(QPointF(state.plotRect.left(), line.pos), QPointF(state.plotRect.right(), line.pos));

	if (state.zeroVisible)
	{
		QPen zeroPen(accent, 1.4);
		zeroPen.setCosmetic(true);
		painter.setPen(zeroPen);
		painter.drawLine(QPointF(state.plotRect.left(), state.zeroY), QPointF(state.plotRect.right(), state.zeroY));
	}

	for (const QPolygonF& segment : state.curves)
	{
		if (segment.size() < 2)
			continue;
		QPainterPath curvePath;
		curvePath.addPolygon(segment);
		// Closed on the segment's own ends, not the plot's: with more than one
		// segment, sweeping to the plot edges would wash the accent under the
		// gap where the metric had no reading. A single full-width segment ends
		// exactly at the plot edges, so the resting magnitude view is unchanged.
		QPainterPath fillPath = curvePath;
		fillPath.lineTo(segment.last().x(), state.zeroY);
		fillPath.lineTo(segment.first().x(), state.zeroY);
		fillPath.closeSubpath();
		painter.fillPath(fillPath, withAlpha(accent, dark ? 30 : 18));

		QPen curvePen(accent, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		curvePen.setCosmetic(true);
		painter.setPen(curvePen);
		painter.drawPath(curvePath);
	}

	QFont labelFont = painter.font();
	labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.0));
	painter.setFont(labelFont);
	painter.setPen(muted);
	const QRectF footer(state.plotRect.left(), state.plotRect.bottom() + 3, state.plotRect.width(), 18);
	painter.drawText(footer, Qt::AlignLeft | Qt::AlignVCenter, state.leftFooterText);
	painter.drawText(footer, Qt::AlignCenter, state.channelText);
	painter.drawText(footer, Qt::AlignRight | Qt::AlignVCenter, state.rightFooterText);
	painter.drawText(QRectF(state.plotRect.left() + 4, state.plotRect.top() + 3, 70, 18),
		Qt::AlignLeft | Qt::AlignVCenter, state.topValueText);
	painter.drawText(QRectF(state.plotRect.left() + 4, state.plotRect.bottom() - 21, 70, 18),
		Qt::AlignLeft | Qt::AlignVCenter, state.bottomValueText);

	if (state.cursorValid)
	{
		QPen cursorPen(withAlpha(muted, 160), 1, Qt::DashLine);
		cursorPen.setCosmetic(true);
		painter.setPen(cursorPen);
		painter.drawLine(QPointF(state.cursor.x(), state.plotRect.top()), QPointF(state.cursor.x(), state.plotRect.bottom()));
		painter.setPen(QPen(accent, 1.4));
		painter.setBrush(QColor(tokens.card));
		painter.drawEllipse(QPointF(state.cursor.x(), state.curveYAtCursor), 3.5, 3.5);
		if (!state.cursorText.isEmpty())
		{
			painter.setPen(QColor(tokens.text));
			painter.drawText(QRectF(state.plotRect.adjusted(0, 2, -6, 0)), Qt::AlignRight | Qt::AlignTop, state.cursorText);
		}
	}
}

void ISkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	// Neutral default: a token-coloured pill, the chosen cell filled in accent,
	// the rest reading as muted text on the surface. Also the heritage look.
	if (state.labels.isEmpty())
		return;

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMin(frame.height() / 2.0, qreal(qMax(2, tokens.borderRadius)));
	QPainterPath pill;
	pill.addRoundedRect(frame, radius, radius);
	painter.fillPath(pill, QColor(tokens.surface));
	painter.setPen(QPen(QColor(state.focused ? tokens.focusRing : tokens.border), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(pill);

	// Drawn at the animated position, so a run through three choices reads as
	// one travelling mark. The inner radius is the outer minus the inset, which
	// is what keeps the two roundings concentric.
	const double inset = 2.0;
	QRectF indicator = state.segmentRect(state.selectionPosition).adjusted(inset, inset, -inset, -inset);
	if (state.enabled)
	{
		QPainterPath mark;
		const qreal innerRadius = qMax(0.0, radius - inset);
		mark.addRoundedRect(indicator, innerRadius, innerRadius);
		painter.fillPath(mark, withAlpha(QColor(tokens.accent), state.pressedIndex == state.selectedIndex ? 220 : 255));
	}

	for (int i = 0; i < state.labels.size(); i++)
	{
		const QRectF cell = state.segmentRect(i);
		QColor ink(i == state.selectedIndex ? tokens.cardSelected : tokens.mutedText);
		if (i == state.selectedIndex)
			ink = QColor(tokens.surface);
		if (!state.enabled)
			ink = withAlpha(QColor(tokens.mutedText), 110);
		else if (i != state.selectedIndex && i == state.hoveredIndex)
			ink = QColor(tokens.text);
		painter.setPen(ink);
		painter.drawText(cell, Qt::AlignCenter, state.labels.at(i));
	}
}

void ISkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
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

	const int pad = GUIHelper::scale(8.0);
	QRectF textRect = cell.adjusted(pad, 0, -pad, 0);

	QFont roleFont = painter.font();
	roleFont.setPixelSize(GUIHelper::scale(9.0));
	painter.setFont(roleFont);
	painter.setPen(roleInk);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleText);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleText);

	// Caret: a small down triangle at the right edge, the shared dropdown cue.
	const qreal caretWidth = GUIHelper::scale(7.0);
	const QPointF caretCenter(textRect.right() - caretWidth / 2.0, cell.center().y() + 0.5);
	QPainterPath caret;
	caret.moveTo(caretCenter + QPointF(-caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(0.0, caretWidth / 2.0));
	caret.closeSubpath();
	painter.fillPath(caret, roleInk);

	QFont valueFont = painter.font();
	valueFont.setPixelSize(GUIHelper::scale(12.0));
	painter.setFont(valueFont);
	painter.setPen(valueInk);
	QString value = state.layoutText;
	if (state.channelCount > 0)
		value += QStringLiteral(" %1").arg(state.channelCount);
	textRect.setLeft(textRect.left() + roleWidth + GUIHelper::scale(6.0));
	textRect.setRight(textRect.right() - caretWidth - GUIHelper::scale(4.0));
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, value);
}

void ISkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
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

	const bool pairVerdict = !state.verdictInputText.isEmpty() || !state.verdictOutputText.isEmpty();
	const bool hasText = pairVerdict || !state.verdictText.isEmpty();
	// A tone without words is a lamp-only verdict (an accepted explicit
	// contract - the selectors already print the pair).
	if (state.verdictRect.isEmpty()
		|| (!hasText && state.tone == VstBusFrameState::Tone::Neutral))
		return;

	QColor lamp(tokens.mutedText);
	switch (state.tone)
	{
	case VstBusFrameState::Tone::Success:
		lamp = QColor(tokens.success);
		break;
	case VstBusFrameState::Tone::Warning:
		lamp = QColor(tokens.warning);
		break;
	case VstBusFrameState::Tone::Critical:
		lamp = QColor(tokens.danger);
		break;
	case VstBusFrameState::Tone::Neutral:
		break;
	}
	if (!state.enabled)
		lamp = withAlpha(lamp, 150);

	const qreal lampRadius = GUIHelper::scale(2.5);
	painter.setPen(Qt::NoPen);
	painter.setBrush(lamp);
	painter.drawEllipse(QPointF(state.verdictRect.left() + lampRadius,
		state.verdictRect.center().y() + 0.5), lampRadius, lampRadius);
	if (!hasText)
		return;

	QFont verdictFont = painter.font();
	verdictFont.setPixelSize(GUIHelper::scale(10.0));
	painter.setFont(verdictFont);
	const QColor ink = state.tone == VstBusFrameState::Tone::Critical ? lamp : muted;
	painter.setPen(ink);
	QRectF textRect(state.verdictRect);
	textRect.setLeft(textRect.left() + lampRadius * 2.0 + GUIHelper::scale(5.0));
	if (pairVerdict)
	{
		// The pair joins over the same painted mark as the joint, so the
		// readout repeats the strip's direction grammar instead of a glyph.
		const QFontMetricsF metrics(verdictFont);
		const qreal inWidth = metrics.horizontalAdvance(state.verdictInputText);
		const qreal markWidth = GUIHelper::scale(14.0);
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.verdictInputText);
		const qreal markLeft = textRect.left() + inWidth + GUIHelper::scale(3.0);
		const qreal midY = textRect.center().y() + 0.5;
		painter.setPen(QPen(ink, 1.1, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(markLeft, midY), QPointF(markLeft + markWidth - GUIHelper::scale(6.0), midY));
		const QPointF head(markLeft + markWidth - GUIHelper::scale(6.0), midY);
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

void ISkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	// Neutral default, deliberately unlike the bus selector: no channel-count
	// suffix and a flatter cell, so a channel pick never reads as a layout
	// pick. The widget sizes the cell from the same fonts used here, so the
	// role, the value and the caret never collide.
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

	const qreal pad = GUIHelper::scale(6.0);
	QRectF textRect = cell.adjusted(pad, 0, -pad, 0);

	QFont roleFont = painter.font();
	roleFont.setPixelSize(GUIHelper::scale(9.0));
	painter.setFont(roleFont);
	painter.setPen(roleInk);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.roleToken);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);

	const qreal caretWidth = GUIHelper::scale(6.0);
	const QPointF caretCenter(textRect.right() - caretWidth / 2.0, cell.center().y() + 0.5);
	QPainterPath caret;
	caret.moveTo(caretCenter + QPointF(-caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(0.0, caretWidth / 2.0));
	caret.closeSubpath();
	painter.fillPath(caret, roleInk);

	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(GUIHelper::scale(11.0));
	painter.setFont(valueFont);
	painter.setPen(valueInk);
	textRect.setLeft(textRect.left() + roleWidth + GUIHelper::scale(5.0));
	textRect.setRight(textRect.right() - caretWidth - GUIHelper::scale(4.0));
	painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, state.valueText);
}

void ISkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	// Neutral default: a quiet sunken tray under the cells, and the fold
	// latch as a small labelled toggle - a state dot plus the FILL token -
	// so the whole-feature fold never reads as one more channel dropdown.
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	if (!state.cellsRect.isNull() && !state.collapsed)
	{
		const QRectF tray = QRectF(state.cellsRect).adjusted(-GUIHelper::scale(4.0), -GUIHelper::scale(3.0),
			GUIHelper::scale(4.0), GUIHelper::scale(3.0));
		painter.setPen(QPen(withAlpha(QColor(tokens.border), 120), 1));
		painter.setBrush(withAlpha(QColor(tokens.surfaceSunken), 90));
		painter.drawRoundedRect(tray, GUIHelper::scale(4.0), GUIHelper::scale(4.0));
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

	const qreal dotRadius = GUIHelper::scale(3.0);
	const QColor dot = state.collapsed ? QColor(tokens.mutedText) : QColor(tokens.accent);
	painter.setPen(Qt::NoPen);
	painter.setBrush(state.enabled ? dot : withAlpha(dot, 140));
	painter.drawEllipse(QPointF(latch.left() + GUIHelper::scale(9.0), latch.center().y() + 0.5),
		dotRadius, dotRadius);

	QFont latchFont = painter.font();
	latchFont.setPixelSize(GUIHelper::scale(9.0));
	painter.setFont(latchFont);
	painter.setPen(QColor(state.collapsed ? tokens.mutedText : tokens.text));
	QRectF labelRect = latch;
	labelRect.setLeft(latch.left() + GUIHelper::scale(15.0));
	painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FILL"));
}

void ISkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
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

FilterPickerView* ISkin::createFilterPicker(QWidget* parent, const SkinTokens& tokens) const
{
	// Neutral default: the shared search-over-sections dropdown, styled by
	// QSS rather than tokens.
	Q_UNUSED(tokens);
	return new DefaultFilterPickerView(parent);
}

ReferenceCardView* ISkin::createReferenceCardView(const QString& kind, QWidget* parent,
	const SkinTokens& tokens) const
{
	Q_UNUSED(tokens);
	// Neutral default: the plain token-styled information hierarchy. kind is
	// unused here because the neutral view derives everything from the state;
	// skins that split their answer per kind branch on it.
	Q_UNUSED(kind);
	return new DefaultReferenceCardView(parent);
}

SubwooferRoutingCardView* ISkin::createSubwooferRoutingCardView(QWidget* parent,
	const SkinTokens& tokens) const
{
	Q_UNUSED(tokens);
	return new DefaultSubwooferRoutingCardView(parent);
}

void ISkin::paintTitleBarChrome(QPainter&, const QRect&, const SkinTokens&) const
{
	// Neutral default: the QSS background is the whole story.
}

void ISkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	if (toolBar == nullptr)
		return;

	// Neutral default: the shared modern stroke icons, tinted with the text
	// token so they follow every skin's dark/light ink. This replaces the
	// legacy .ico set the .ui file still references (kept there so a skin
	// could deliberately return to it).
	const QColor ink(tokens.text);
	toolBar->setIconSize(GUIHelper::scale(QSize(18, 18)));
	for (QAction* action : toolBar->actions())
	{
		if (action->objectName() == QStringLiteral("actionNew"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/file-new.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionOpen"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionSave"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/save.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionUndo"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/undo.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionRedo"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/redo.svg"), ink, 18));
	}
}

void ISkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
	if (dialog == nullptr)
		return;

	// Neutral default: the shared modern stroke set on the dialog's own
	// navigation buttons, tinted with the text token. QFileDialog seeds these
	// from QStyle::standardIcon, which is the last place the platform style's
	// 2005-era pictograms would survive inside a skinned session.
	const QColor ink(tokens.text);
	const struct { const char* name; const char* resource; } buttons[] = {
		{ "backButton", ":/icons/modern/nav-back.svg" },
		{ "forwardButton", ":/icons/modern/nav-forward.svg" },
		{ "toParentButton", ":/icons/modern/folder-up.svg" },
		{ "newFolderButton", ":/icons/modern/folder-new.svg" },
		{ "listModeButton", ":/icons/modern/view-list.svg" },
		{ "detailModeButton", ":/icons/modern/view-detail.svg" },
	};
	for (const auto& button : buttons)
	{
		QToolButton* toolButton = dialog->findChild<QToolButton*>(QLatin1String(button.name));
		if (toolButton != nullptr)
		{
			toolButton->setIcon(GUIHelper::tintedIcon(QLatin1String(button.resource), ink, 18));
			toolButton->setIconSize(GUIHelper::scale(QSize(18, 18)));
		}
	}
}
