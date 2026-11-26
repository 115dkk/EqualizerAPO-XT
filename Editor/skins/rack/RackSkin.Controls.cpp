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
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/widgets/cards/VSTSlotFillRail.h"
#include "RackSkinDetail.h"

void RackSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	painter.setRenderHint(QPainter::Antialiasing);

	// Work inside a centred square (promoted legacy dials are 100x66).
	const QRectF inner = QRectF(rect).adjusted(4, 4, -4, -4);
	const qreal side = qMin(inner.width(), inner.height());
	const QPointF center = inner.center();
	const qreal scaleRadius = side / 2.0;        // printed panel scale
	const qreal bodyRadius = scaleRadius - 9.0;  // physical knob body

	// Value geometry matches AudioKnob's input mapping: minimum at 135
	// degrees (bottom-left), 270-degree clockwise sweep, maximum at the
	// bottom-right, dead zone across the bottom.
	auto pointAt = [center](qreal ratio, qreal radius) {
		const qreal a = qDegreesToRadians(135.0 + 270.0 * ratio);
		return center + QPointF(qCos(a) * radius, qSin(a) * radius);
	};

	// Scale ticks are printed on the PANEL around the knob, never on the
	// knob - they do not move and there is no value arc; the pointer alone
	// carries the value, as on real hardware. The printing must read at
	// a glance, so the end stops are majors in full panel ink while the
	// intermediates stay muted; the bipolar neutral (0 dB at 12 o'clock) is
	// the boldest mark on the plate - the centre detent, in amber, longer
	// and heavier than any other tick.
	const QColor inkBase(tokens.mutedText);
	const QColor inkStrong(tokens.text);
	const int inkAlpha = state.enabled ? 235 : 90;
	const int minorAlpha = state.enabled ? 165 : 70;
	for (int i = 0; i <= 10; i++)
	{
		const qreal ratio = i / 10.0;
		const bool centerTick = state.bipolar && i == 5;
		const bool major = i == 0 || i == 10 || centerTick;
		QColor ink = centerTick ? QColor(tokens.accent) : (major ? inkStrong : inkBase);
		ink.setAlpha(major ? inkAlpha : minorAlpha);
		const qreal innerRadius = centerTick ? bodyRadius + 2.0 : bodyRadius + 3.5;
		const qreal outerRadius = centerTick ? scaleRadius + 2.0 : (major ? scaleRadius + 0.5 : scaleRadius - 1.5);
		painter.setPen(QPen(ink, centerTick ? 3.0 : (major ? 2.0 : 1.0), Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(pointAt(ratio, innerRadius), pointAt(ratio, outerRadius));
	}

	// Bipolar knobs print the cut/boost glyphs in the dead zone under the
	// scale ends, like a gain pot's faceplate. Unipolar knobs stay plain, so
	// the two kinds never look alike. The glyphs are engraved (contrast
	// pass offset one pixel down) in full panel ink, large enough to read at
	// the gallery's knob size.
	if (state.bipolar)
	{
		QFont glyphFont(tokens.fontFamily);
		glyphFont.setPixelSize(12);
		glyphFont.setBold(true);
		painter.setFont(glyphFont);
		const QPointF minusAt = pointAt(-0.07, scaleRadius - 2.5);
		const QPointF plusAt = pointAt(1.07, scaleRadius - 2.5);
		const QRectF minusRect(minusAt.x() - 7, minusAt.y() - 7, 14, 14);
		const QRectF plusRect(plusAt.x() - 7, plusAt.y() - 7, 14, 14);
		painter.setPen(RackPalette::EngraveRelief(dark));
		painter.drawText(minusRect.translated(0, 1), Qt::AlignCenter, QStringLiteral("-"));
		painter.drawText(plusRect.translated(0, 1), Qt::AlignCenter, QStringLiteral("+"));
		painter.setPen(withAlpha(inkStrong, inkAlpha));
		painter.drawText(minusRect, Qt::AlignCenter, QStringLiteral("-"));
		painter.drawText(plusRect, Qt::AlignCenter, QStringLiteral("+"));
	}

	// Knob body: bakelite (dark mode) / machined aluminium (light mode) with
	// an offset highlight suggesting the work light.
	QRadialGradient bodyGrad(center - QPointF(bodyRadius * 0.4, bodyRadius * 0.4), bodyRadius * 2.2);
	const QColor cap(tokens.card);
	if (dark)
	{
		bodyGrad.setColorAt(0.0, cap.lighter(190));
		bodyGrad.setColorAt(0.6, cap.lighter(115));
		bodyGrad.setColorAt(1.0, cap.darker(160));
	}
	else
	{
		bodyGrad.setColorAt(0.0, RackPalette::KnobAluminiumHighlight);
		bodyGrad.setColorAt(0.6, RackPalette::KnobAluminiumBody);
		bodyGrad.setColorAt(1.0, RackPalette::KnobAluminiumEdge);
	}
	// Keyboard focus recolours the body's own rim (shared article 8): one
	// line on the outline the knob already has, never a ring of its own.
	const bool focusRim = state.focused && state.enabled;
	painter.setPen(QPen(focusRim ? withAlpha(QColor(tokens.focusRing), 220) : RackPalette::KnobRim(dark), 1));
	painter.setBrush(bodyGrad);
	painter.drawEllipse(center, bodyRadius, bodyRadius);

	// Machined cap step and the specular arc on its top edge.
	const qreal capRadius = bodyRadius - 3.5;
	painter.setPen(QPen(RackPalette::shadow(dark ? 90 : 50), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, capRadius, capRadius);
	painter.setPen(QPen(RackPalette::light(dark ? 70 : 150), 1.2));
	painter.drawArc(QRectF(center.x() - capRadius, center.y() - capRadius, capRadius * 2, capRadius * 2), 60 * 16, 60 * 16);

	// The pointer: a physical painted line. Hover/drag turns it amber (the
	// hand is on the knob), disabled grays it out. A recessed shadow
	// pass underneath and a tip reaching the knob skirt keep the pointer
	// readable against the printed scale at any angle.
	QColor pointerColor;
	if (!state.enabled)
		pointerColor = withAlpha(inkBase, 130);
	else if (state.dragging || state.hovered)
		pointerColor = QColor(tokens.accent);
	else
		pointerColor = RackPalette::KnobPointer(dark);
	const QPointF pointerBase = pointAt(state.ratio, bodyRadius * 0.28);
	const QPointF pointerTip = pointAt(state.ratio, bodyRadius - 1.8);
	painter.setPen(QPen(RackPalette::shadow(state.enabled ? (dark ? 150 : 90) : 50), 3.6, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(pointerBase, pointerTip);
	painter.setPen(QPen(pointerColor, 2.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(pointerBase, pointerTip);

	// Hover/drag: the knob rim catches the light. While the rim already
	// carries the focus colour it stays the one edge.
	if (state.enabled && (state.hovered || state.dragging) && !focusRim)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.accent), 90), 1.4));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(center, bodyRadius + 0.8, bodyRadius + 0.8);
	}

	// Powered-down knob: dim film over the body.
	if (!state.enabled)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(RackPalette::KnobPoweredDownFilm(dark));
		painter.drawEllipse(center, bodyRadius, bodyRadius);
	}

	// No value window on the knob itself: a display pane across the cap is
	// not buildable hardware and it would cut the pointer line in half.
	// The value lives in the card's own LED
	// display (EditableValue) beside the knob; state.valueText is
	// deliberately unused here.
}

// Rack's pick-one control is a slide switch with its options printed on the
// faceplate above it: the legends are lettering on the plate, one centred in
// each cell, and under them a short recessed slot holds one small slider
// knob that rests under the chosen side. It replaces a recessed bank of two
// machined key caps with grain, a lamp in each cap and an interlock slide
// beneath them. The maintainer judged that bank far too heavy for choosing
// one of two (2026-11-26, the all-pass card's ORDER and graph switches):
// tone it down, keep it Rack, and make it read as a hardware part that
// selects one of two. A slide switch is that part, with a fraction of the
// metal.
//
// The knob's side is the only thing that says which option is chosen (shared
// article 1: a state is said once), so the two legends print in the same ink
// at the same weight whatever the selection, as printing on a real panel
// does. The switch is the same part at the same size wherever the control
// appears, centred under the legends; more than two options lengthen the
// slot and give the knob evenly spaced rests.
void RackSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	if (state.labels.isEmpty() || state.rect.width() < 10 || state.rect.height() < 8)
		return;

	const bool dark = skinIsDark(tokens);
	const QColor panel(tokens.card);
	const QColor bodyInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	// Light and shadow instead of palette entries: a machined recess is the one
	// work light falling across metal. The dark side keeps the cream finish warm
	// by mixing the plate's own ink toward black rather than laying a neutral
	// grey over it - this skin's shadows are never cold.
	const QColor shadowInk = dark ? RackPalette::shadow(255) : mixColor(bodyInk, RackPalette::shadow(255), 0.35);
	const QColor lightInk = RackPalette::light(255);
	// One light colour for the whole switch, pointer and keyboard alike: bronze
	// on the dark finish (the colour every cap that answers the pointer or holds
	// focus wears there), the focus colour on the light one, as the VST fill
	// block's caps do. The part has no lamp, so amber has no place on it in the
	// dark.
	const QColor engagedEdge(dark ? RackSkinDetail::darkEngagedFrame() : tokens.focusRing);
	const int positions = int(state.labels.size());

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	QFont legendFont(tokens.fontFamily);
	legendFont.setPixelSize(10);
	legendFont.setBold(true);
	const QFontMetricsF legendMetrics(legendFont);

	// The stack: the legend line, a 2px gap, the 8px slot. It is centred in the
	// widget's height, so the taller instances on the cards share the slack and
	// the standalone 26px control keeps a pixel or two above and below.
	constexpr qreal slotHeight = 8.0;
	constexpr qreal legendGap = 2.0;
	constexpr qreal knobWidth = 14.0;
	constexpr qreal knobInset = 1.0;
	// One rest to the next: a two-position switch has a 30px slot.
	constexpr qreal knobTravel = 14.0;
	const QRectF frame(state.rect);
	const qreal legendHeight = qCeil(legendMetrics.height());
	const qreal stackHeight = legendHeight + legendGap + slotHeight;
	const qreal stackTop = frame.top() + qMax(0.0, qreal(qFloor((frame.height() - stackHeight) / 2.0)));

	// The legends: lettering printed on the plate, engraved as written (they
	// are translated UI strings, not hardware printing: no uppercasing, no
	// tracking, the plate's channel caption rule). A powered-down unit keeps
	// its printing, faded, as every print on this machine fades.
	painter.setFont(legendFont);
	const QColor legendInk = state.enabled ? bodyInk : withAlpha(mutedInk, 150);
	for (int i = 0; i < positions; i++)
	{
		const QRectF cell = state.segmentRect(i);
		const QRectF legendRect(cell.left() + 2.0, stackTop, qMax(0.0, cell.width() - 4.0), legendHeight);
		RackSkinDetail::engraveText(painter, legendRect, Qt::AlignHCenter | Qt::AlignVCenter,
			legendMetrics.elidedText(state.labels.at(i), Qt::ElideRight, legendRect.width()), legendInk, dark);
	}

	// The slot: a short recessed channel with rounded ends, cut in the plate.
	// Its floor is the faceplate itself in shadow (darker() scales value and
	// keeps the finish's warmth), never a new colour, and it wears the recess
	// grammar the LCD wells and the patchbay's button field wear: an overhang
	// shadow under the top lip, the work light caught on the lower one, inside
	// an outline that parts it from the plate on all four sides (invariant 12).
	const qreal slotWidth = qMin(knobWidth + 2.0 * knobInset + knobTravel * (positions - 1), frame.width() - 2.0);
	const QRectF slot(qFloor(frame.center().x() - slotWidth / 2.0), stackTop + legendHeight + legendGap,
		qRound(slotWidth), slotHeight);
	const QRectF slotEdge = slot.adjusted(0.5, 0.5, -0.5, -0.5);
	QPainterPath slotPath;
	slotPath.addRoundedRect(slotEdge, slotEdge.height() / 2.0, slotEdge.height() / 2.0);
	painter.setPen(Qt::NoPen);
	painter.fillPath(slotPath, panel.darker(dark ? 185 : 135));
	{
		QPainterStateGuard slotState(&painter);
		painter.setClipPath(slotPath);
		QLinearGradient overhang(slot.topLeft(), QPointF(slot.left(), slot.top() + 3.0));
		overhang.setColorAt(0.0, withAlpha(shadowInk, dark ? 170 : 110));
		overhang.setColorAt(1.0, withAlpha(shadowInk, 0));
		painter.fillRect(QRectF(slot.left(), slot.top(), slot.width(), 3.0), overhang);
		painter.setPen(QPen(withAlpha(lightInk, dark ? 34 : 150), 1));
		painter.drawLine(QPointF(slot.left() + 3.0, slot.bottom() - 1.5), QPointF(slot.right() - 3.0, slot.bottom() - 1.5));
	}
	// Keyboard focus recolours the slot's own outline (shared article 8): one
	// line on an edge the switch already draws, never a box of its own. The
	// knob inside does not take a second lit edge while the slot holds one.
	const bool focusEdge = state.enabled && state.focused;
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(focusEdge ? withAlpha(engagedEdge, 220) : withAlpha(shadowInk, dark ? 215 : 135), 1));
	painter.drawPath(slotPath);

	// The slider knob: a small raised cap riding in the slot, lighter than the
	// floor on both finishes, with three fine grip lines. It rests at the left
	// end for the first option and at the right end for the last, and it is
	// the one part that moves, so it alone follows selectionPosition while the
	// selection travels. On the dark finish the cap is the steel plate mixed
	// toward the warm ivory ink, so it reads at a glance against the dark floor
	// without turning cold. The cream finish's plate colour is already at full
	// value, so its cap steps downward from the plate - lighter() on it would
	// return the same colour and flatten the cap.
	const double travel = qBound(0.0, state.selectionPosition, double(positions - 1));
	const qreal knobRoom = slot.width() - 2.0 * knobInset - knobWidth;
	const qreal knobLeft = slot.left() + knobInset
		+ (positions > 1 ? knobRoom * travel / double(positions - 1) : knobRoom / 2.0);
	const QRectF knob(qRound(knobLeft), slot.top() + knobInset, knobWidth, slotHeight - 2.0 * knobInset);
	const QRectF knobEdge = knob.adjusted(0.5, 0.5, -0.5, -0.5);
	QLinearGradient face(knob.topLeft(), knob.bottomLeft());
	if (state.enabled)
	{
		face.setColorAt(0.0, dark ? mixColor(panel, bodyInk, 0.42) : panel);
		face.setColorAt(1.0, dark ? mixColor(panel, bodyInk, 0.26) : panel.darker(110));
	}
	else
	{
		// Powered down the cap stays, in the floor's own shadow: no light on it.
		face.setColorAt(0.0, dark ? mixColor(panel, bodyInk, 0.20) : panel.darker(108));
		face.setColorAt(1.0, dark ? mixColor(panel, bodyInk, 0.13) : panel.darker(118));
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(face);
	painter.drawRoundedRect(knobEdge, 1.5, 1.5);
	if (state.enabled)
	{
		// The work light on the cap's top chamfer, inside its outline.
		painter.setPen(QPen(withAlpha(lightInk, dark ? 60 : 170), 1));
		painter.drawLine(QPointF(knob.left() + 2.0, knob.top() + 1.5), QPointF(knob.right() - 2.0, knob.top() + 1.5));
	}
	painter.setPen(QPen(withAlpha(shadowInk, state.enabled ? (dark ? 170 : 105) : (dark ? 110 : 70)), 1));
	for (int line = -1; line <= 1; line++)
	{
		const qreal x = knob.left() + 7.5 + line * 3.0;
		painter.drawLine(QPointF(x, knob.top() + 2.0), QPointF(x, knob.bottom() - 1.5));
	}

	// The hand on the switch: the knob's own outline warms, the way the bus and
	// fill caps answer the pointer. It warms when the pointer is over an option
	// the knob is not resting under (pressing there moves it), and harder while
	// the button is held there. While the slot holds the focus edge the knob
	// keeps its plain outline, so the switch never shows a lit edge inside a lit
	// edge (the nested boxes of the 2026-11-18 ruling).
	const bool aimed = state.enabled && !focusEdge && state.hoveredIndex >= 0 && state.hoveredIndex != state.selectedIndex;
	const bool held = state.enabled && !focusEdge && state.pressedIndex >= 0 && state.pressedIndex != state.selectedIndex;
	QColor knobOutline = withAlpha(shadowInk, dark ? 220 : 130);
	if (held)
		knobOutline = withAlpha(engagedEdge, 220);
	else if (aimed)
		knobOutline = withAlpha(engagedEdge, dark ? 235 : 190);
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(knobOutline, 1));
	painter.drawRoundedRect(knobEdge, 1.5, 1.5);
}

namespace
{
// The latch-cap ground shared by both bus selectors: a raised cap at rest
// (top highlight, faint vertical fall-off), latched down while pressed or
// while its menu holds it (inverted bevel, face sunk one step). The same
// component law as the Device/Channel caps - a latching selection is a
// square button, never a dial.
void paintRackBusCap(QPainter& painter, const QRectF& cap, bool down, bool enabled,
	const SkinTokens& tokens, bool dark)
{
	QLinearGradient face(cap.topLeft(), cap.bottomLeft());
	if (down)
	{
		face.setColorAt(0.0, withAlphaF(QColor(Qt::black), dark ? 0.28 : 0.16));
		face.setColorAt(1.0, withAlphaF(QColor(Qt::black), dark ? 0.14 : 0.08));
	}
	else
	{
		face.setColorAt(0.0, withAlphaF(QColor(Qt::white), enabled ? (dark ? 0.07 : 0.35) : 0.03));
		face.setColorAt(1.0, withAlphaF(QColor(Qt::black), dark ? 0.12 : 0.06));
	}
	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.setBrush(face);
	painter.drawRoundedRect(cap, 2.0, 2.0);

	// Bevel: light on top while raised, shadow on top while latched down.
	painter.setPen(QPen(down
		? withAlphaF(QColor(Qt::black), 0.30)
		: withAlphaF(QColor(Qt::white), enabled ? (dark ? 0.10 : 0.55) : 0.04), 1));
	painter.drawLine(QPointF(cap.left() + 2.0, cap.top() + 1.5),
		QPointF(cap.right() - 2.0, cap.top() + 1.5));
}
}

// The VST3 bus contract as service-panel hardware: engraved IN/OUT
// markings on the plate, the layouts on latching button caps, mounted in
// the recessed sub-panel paintVstBusFrame digs. Powered down (VST2, not
// loaded) the caps stay - hardware remains - but the engraving dims and
// nothing lights.
void RackSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QRectF rect(state.rect);

	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(9);
	roleFont.setBold(true);
	roleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
	painter.setFont(roleFont);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);
	const QColor engraveInk = withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);
	RackSkinDetail::engraveText(painter,
		QRectF(rect.left(), rect.top(), roleWidth, rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, state.roleToken, engraveInk, dark);

	QRectF cap(rect.left() + roleWidth + 5.0, rect.top() + 0.5,
		rect.width() - roleWidth - 5.5, rect.height() - 1.0);
	const bool down = state.pressed || state.menuOpen;
	paintRackBusCap(painter, cap, down, state.enabled, tokens, dark);
	if (state.enabled && (state.hovered || state.focused))
	{
		// The lamp grammar for attention: the border warms amber; never a lift.
		painter.setPen(QPen(withAlpha(QColor(tokens.accent), state.focused ? 220 : 140), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(cap, 2.0, 2.0);
	}

	// The cap printing: mono value, the width as a small side figure. The
	// latched cap carries its print down with it.
	const qreal drop = down ? 1.0 : 0.0;
	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(12);
	painter.setFont(valueFont);
	QColor print(state.enabled ? tokens.text : tokens.mutedText);
	QRectF printRect = cap.adjusted(6.0, drop, -5.0, drop);
	painter.setPen(print);
	painter.drawText(printRect, Qt::AlignLeft | Qt::AlignVCenter, state.layoutText);
	if (state.channelCount > 0)
	{
		const qreal valueWidth = QFontMetricsF(valueFont).horizontalAdvance(state.layoutText);
		QFont countFont(tokens.monoFontFamily);
		countFont.setPixelSize(9);
		painter.setFont(countFont);
		painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
		painter.drawText(QRectF(printRect.left() + valueWidth + 4.0, printRect.top(),
			printRect.width() - valueWidth - 4.0, printRect.height()),
			Qt::AlignLeft | Qt::AlignVCenter, QString::number(state.channelCount));
	}
}

// The caps mount directly on the faceplate. An earlier round framed the
// whole strip in a recessed sub-panel; the outline read as cramped around
// the engravings and was judged off - the caps are already components, and
// hardware does not frame two buttons (maintainer decision, r2). The
// signal direction is engraved into the plate between the caps, and the
// verdict is a bezel-set panel LED - paintLed, the only lamp this skin
// builds - beside a small engraving.
void RackSkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);

	// The engraved direction mark between the caps: the recess-edge light
	// pass one pixel down, then the ink - the engraving formula as a line.
	const QColor arrowInk = withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);
	const qreal midY = state.jointRect.center().y() + 0.5;
	const QPointF tail(state.jointRect.left() + 4.0, midY);
	const QPointF head(state.jointRect.right() - 4.0, midY);
	painter.setPen(QPen(RackPalette::EngraveRelief(dark), 1.2, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(tail + QPointF(0, 1), head + QPointF(0, 1));
	painter.drawLine(head + QPointF(0, 1), head + QPointF(-3.0, -2.0));
	painter.drawLine(head + QPointF(0, 1), head + QPointF(-3.0, 4.0));
	painter.setPen(QPen(arrowInk, 1.2, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(tail, head);
	painter.drawLine(head, head + QPointF(-3.0, -3.0));
	painter.drawLine(head, head + QPointF(-3.0, 3.0));

	if (!state.showVerdict)
		return;

	// The warning tone lights in the accent rather than the warning token.
	const QColor led(state.tone == VstBusFrameState::Tone::Warning ? tokens.accent : tokens.*state.toneToken);
	const QPointF ledCenter(state.verdictRect.left() + 4.0, state.verdictRect.center().y() + 0.5);
	RackSkinDetail::paintLed(painter, ledCenter, 3.2, led, state.lit, dark);

	if (!state.hasText)
		return;

	QFont engraveFont(tokens.monoFontFamily);
	engraveFont.setPixelSize(10);
	engraveFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
	painter.setFont(engraveFont);
	const QColor textInk = state.tone == VstBusFrameState::Tone::Critical && state.enabled
		? QColor(tokens.danger) : withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);
	QRectF textRect(state.verdictRect);
	textRect.setLeft(ledCenter.x() + 9.0);
	QString text;
	if (state.pairVerdict)
		text = state.verdictInputText + QStringLiteral(" - ") + state.verdictOutputText;
	else
		text = state.verdictText;
	RackSkinDetail::engraveText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetricsF(engraveFont).elidedText(text, Qt::ElideRight, textRect.width()), textInk, dark);
}

// The height of a value cap and of the FILL cap in the patch block. Two
// under the split rails' cell: at 20 the block's two rows of caps read
// heavier than the bus selectors above them.
constexpr int rackFillBlockCellHeight = 18;

namespace
{
// The fill cell's fonts, shared by the painter and the size it answers:
// an engraved bold, letter-spaced role and the mono channel print.
QFont rackFillRoleFont(const SkinTokens& tokens)
{
	QFont font(tokens.fontFamily);
	font.setPixelSize(9);
	font.setBold(true);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	return font;
}

QFont rackFillValueFont(const SkinTokens& tokens)
{
	QFont font(tokens.monoFontFamily);
	font.setPixelSize(12);
	return font;
}

// The block's row captions, engraved as the bus strip engraves its IN/OUT.
QFont rackFillCaptionFont(const SkinTokens& tokens)
{
	QFont font(tokens.fontFamily);
	font.setPixelSize(9);
	font.setBold(true);
	font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
	return font;
}

// A value cap's width in the block: the channel plus 5 either side, and
// never narrower than the cap is tall. The caps were first all as wide as
// "LFE" with 7 of air, which made a row of one-letter channels look fat
// (maintainer, 2026-11-22); a cap is as wide as what it prints.
qreal rackFillCapWidth(const QString& value, const SkinTokens& tokens)
{
	const QFontMetricsF metrics(rackFillValueFont(tokens));
	return qMax(metrics.horizontalAdvance(value) + 10.0, qreal(rackFillBlockCellHeight));
}

// The edge a cap of the fill block wears for pointer and keyboard: bronze
// on the dark finish, amber on the light one (the latched-cap colours of
// this unit).
QColor rackFillAttentionEdge(const SkinTokens& tokens, bool dark)
{
	return QColor(dark ? RackSkinDetail::darkEngagedFrame() : tokens.accent);
}
}

QSize RackSkin::vstSlotFillCellSize(const QString& role, const QString& value, const SkinTokens& tokens) const
{
	// The painter's layout: the engraved role, 5 to the cap, and the channel
	// printed inside the cap with 6 before it and 5 after (the cap itself
	// ends 0.5 short of the cell). The cap has no caret.
	const qreal roleWidth = QFontMetricsF(rackFillRoleFont(tokens)).horizontalAdvance(role);
	const qreal valueWidth = QFontMetricsF(rackFillValueFont(tokens)).horizontalAdvance(value);
	return QSize(qCeil(roleWidth + 5.0 + 6.0 + valueWidth + 5.0 + 0.5), VSTSlotFillRail::cellHeight());
}

void RackSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QColor amber(tokens.accent);
	const QColor bodyInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	const QRectF rect(state.rect);

	if (state.roleInBlock)
	{
		// In the patch block the role is engraved on the block's role line;
		// the cap fills the column and prints the channel centred. Same cap
		// as the bus selectors (verdict r7), and the same attention edge as
		// the block's FILL cap.
		const QRectF cap = rect.adjusted(0.5, 0.5, -0.5, -0.5);
		const bool down = state.enabled && (state.pressed || state.menuOpen);
		paintRackBusCap(painter, cap, down, state.enabled, tokens, dark);
		if (state.missingChannel)
		{
			painter.setPen(QPen(QColor(tokens.danger), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(cap, 2.0, 2.0);
		}
		else if (state.enabled && (state.hovered || state.focused))
		{
			painter.setPen(QPen(withAlpha(rackFillAttentionEdge(tokens, dark), state.focused ? 220 : 140), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(cap, 2.0, 2.0);
		}
		painter.setFont(rackFillValueFont(tokens));
		QColor print(state.silent || state.defaulted ? mutedInk : bodyInk);
		if (state.missingChannel)
			print = QColor(tokens.danger);
		if (!state.enabled)
			print = withAlpha(mutedInk, 150);
		painter.setPen(print);
		painter.drawText(cap.translated(0.0, down ? 1.0 : 0.0), Qt::AlignHCenter | Qt::AlignVCenter, state.valueText);
		return;
	}

	const QFont roleFont = rackFillRoleFont(tokens);
	painter.setFont(roleFont);
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(state.roleToken);
	RackSkinDetail::engraveText(painter,
		QRectF(rect.left(), rect.top(), roleWidth, rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter, state.roleToken,
		withAlpha(mutedInk, state.enabled ? 255 : 150), dark);

	// One relief language across the card (verdict r7): the fill cell wears
	// exactly the bus selectors' cap - the modest latch-down face, amber
	// warmth for attention - so nothing on this unit pops out while its
	// neighbor sinks in. What tells it apart from IN/OUT is its engraved
	// per-slot role and the channel it prints, not a different mechanism.
	const QRectF cap(rect.left() + roleWidth + 5.0, rect.top() + 0.5,
		rect.width() - roleWidth - 5.5, rect.height() - 1.0);
	const bool down = state.enabled && (state.pressed || state.menuOpen);
	paintRackBusCap(painter, cap, down, state.enabled, tokens, dark);
	if (state.missingChannel)
	{
		painter.setPen(QPen(QColor(tokens.danger), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(cap, 2.0, 2.0);
	}
	else if (state.enabled && (state.hovered || state.focused))
	{
		painter.setPen(QPen(withAlpha(amber, state.focused ? 220 : 140), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(cap, 2.0, 2.0);
	}

	const qreal drop = down ? 1.0 : 0.0;
	painter.setFont(rackFillValueFont(tokens));
	QColor print(state.silent || state.defaulted ? mutedInk : bodyInk);
	if (state.missingChannel)
		print = QColor(tokens.danger);
	if (!state.enabled)
		print = withAlpha(mutedInk, 150);
	painter.setPen(print);
	painter.drawText(cap.adjusted(6.0, drop, -5.0, drop),
		Qt::AlignLeft | Qt::AlignVCenter, state.valueText);
}

VstSlotFillArrangement RackSkin::vstSlotFillArrangement() const
{
	return VstSlotFillArrangement::JoinedBlockBelowBody;
}

VstSlotFillBlockMetrics RackSkin::vstSlotFillBlockMetrics(bool collapsed, const SkinTokens& tokens) const
{
	Q_UNUSED(collapsed);
	VstSlotFillBlockMetrics metrics;
	metrics.cellHeight = rackFillBlockCellHeight;
	metrics.roleLineHeight = 11;
	metrics.roleGap = 1;
	metrics.groupGap = 8;
	metrics.cellGap = 6;
	const QFontMetricsF caption(rackFillCaptionFont(tokens));
	metrics.captionWidth = qCeil(qMax(caption.horizontalAdvance(QStringLiteral("IN")),
		caption.horizontalAdvance(QStringLiteral("OUT"))));
	metrics.captionGap = 7;
	metrics.padding = QMargins(8, 5, 9, 5);
	metrics.latchPlacement = VstSlotFillBlockMetrics::LatchPlacement::LeftColumn;
	// The FILL cap: the pilot LED in its first 14, the engraved legend, 7
	// of air after it.
	metrics.latchWidth = qCeil(14.0
		+ QFontMetricsF(rackFillRoleFont(tokens)).horizontalAdvance(QStringLiteral("FILL")) + 7.0);
	metrics.latchHeight = metrics.cellHeight;
	metrics.latchGap = 10;
	metrics.shareRoleLine = true;
	metrics.rolesAboveRows = false;
	return metrics;
}

int RackSkin::vstSlotFillBlockColumnWidth(const QString& role, const QString& value, bool output,
	const SkinTokens& tokens) const
{
	Q_UNUSED(output);
	const qreal roleWidth = QFontMetricsF(rackFillRoleFont(tokens)).horizontalAdvance(role);
	return qCeil(qMax(roleWidth + 2.0, rackFillCapWidth(value, tokens)));
}

// The patch block: one recessed band on the module face, cut to what it
// holds and edged on all four sides in the order every rack recess uses
// (shadow on the top and left edges, the lit lip on the bottom and right).
// In it the IN row of value caps, the slot roles engraved once between the
// rows (or per row when the two slot lists differ), the OUT row, the IN/OUT
// row heads engraved as the bus strip engraves its IN/OUT, and the FILL cap
// at the left: the same cap as the values, its state on the pilot LED.
void RackSkin::paintVstSlotFillBlock(QPainter& painter, const VstSlotFillBlockState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QColor engraveInk = withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);

	if (!state.bandRect.isNull())
	{
		const QRectF band(state.bandRect);
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlphaF(QColor(Qt::black), dark ? 0.16 : 0.05));
		painter.drawRect(band);
		const qreal l = band.left() + 0.5;
		const qreal r = band.right() - 0.5;
		const qreal t = band.top() + 0.5;
		const qreal b = band.bottom() - 0.5;
		painter.setPen(QPen(withAlphaF(QColor(Qt::black), dark ? 0.45 : 0.16), 1));
		painter.drawLine(QPointF(l, t), QPointF(r, t));
		painter.drawLine(QPointF(l, t), QPointF(l, b));
		painter.setPen(QPen(withAlphaF(QColor(Qt::white), dark ? 0.07 : 0.5), 1));
		painter.drawLine(QPointF(l + 1.0, b), QPointF(r, b));
		painter.drawLine(QPointF(r, t + 1.0), QPointF(r, b));
	}

	painter.setFont(rackFillCaptionFont(tokens));
	for (const VstSlotFillBlockState::Caption& caption : state.captions)
	{
		RackSkinDetail::engraveText(painter, QRectF(caption.rect), Qt::AlignRight | Qt::AlignVCenter,
			caption.output ? QStringLiteral("OUT") : QStringLiteral("IN"), engraveInk, dark);
	}
	painter.setFont(rackFillRoleFont(tokens));
	for (const VstSlotFillBlockState::Role& role : state.roles)
	{
		RackSkinDetail::engraveText(painter, QRectF(role.rect), Qt::AlignHCenter | Qt::AlignVCenter,
			role.role, engraveInk, dark);
	}

	if (state.latchRect.isNull() || !state.latchActive)
		return;

	// The fold is a value cap of the same face, border and height (one
	// relief language, verdict r7); its state lives in the pilot LED. Raised
	// = folded, latched down = expanded, as every switch on this unit.
	const QRectF cap = QRectF(state.latchRect).adjusted(0.5, 0.5, -0.5, -0.5);
	const bool down = !state.collapsed || state.latchPressed;
	paintRackBusCap(painter, cap, down, state.enabled, tokens, dark);
	if (state.enabled && (state.latchHovered || state.latchFocused))
	{
		painter.setPen(QPen(withAlpha(rackFillAttentionEdge(tokens, dark), state.latchFocused ? 220 : 140), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(cap, 2.0, 2.0);
	}
	const qreal drop = down ? 1.0 : 0.0;
	RackSkinDetail::paintLed(painter, QPointF(cap.left() + 7.5, cap.center().y() + 0.5 + drop), 2.6,
		QColor(tokens.accent), state.enabled && !state.collapsed, dark);
	painter.setFont(rackFillRoleFont(tokens));
	RackSkinDetail::engraveText(painter, cap.adjusted(14.0, drop, -3.0, drop),
		Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FILL"),
		withAlpha(QColor(state.collapsed ? tokens.mutedText : tokens.text), state.enabled ? 255 : 150), dark);
}
