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

namespace
{
// A choice among three or more is a bank of interlocked keys: a recessed
// bank holding one machined key cap per option, each with its lamp, and the
// interlock slide under them. A slide switch says one of two by which end
// its knob sits at; with three or more legends spread across the control a
// short switch's rests no longer stand under the legends they mean, while a
// key sits under its own legend whatever the count. The bank is also the
// form the focus verdict of 2026-11-18 was given on (docs/skins/README.md,
// shared article 8).
void paintRackKeyBank(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens)
{
	if (state.labels.isEmpty() || state.rect.width() < 10 || state.rect.height() < 8)
		return;

	const bool dark = skinIsDark(tokens);
	const QColor panel(tokens.card);
	const QColor amber(tokens.accent);
	const QColor bodyInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	// Light and shadow instead of palette entries: a machined recess is the one
	// work light falling across metal. The dark side keeps the cream finish warm
	// by mixing the plate's own ink toward black rather than laying a neutral
	// grey over it - this skin's shadows are never cold.
	const QColor shadowInk = dark ? RackPalette::shadow(255) : mixColor(bodyInk, RackPalette::shadow(255), 0.35);
	const QColor lightInk = RackPalette::light(255);
	const QColor grainInk = dark ? lightInk : mixColor(bodyInk, panel, 0.30);

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMin(qreal(qMax(2, tokens.borderRadius)), frame.height() / 2.0);
	QPainterPath well;
	well.addRoundedRect(frame, radius, radius);

	// The bank sits in a RECESSED sub-panel. Its floor is the faceplate itself
	// in shadow (darker() scales value and keeps the finish's warmth), never a
	// new colour, and the recess wears the grammar the LCD wells and the
	// patchbay's button field wear: an overhang shadow on the top lip, the work
	// light caught on the lower one.
	painter.setPen(Qt::NoPen);
	painter.fillPath(well, panel.darker(dark ? 178 : 122));

	QPainterStateGuard wellState(&painter);
	painter.setClipPath(well);

	QLinearGradient overhang(frame.topLeft(), QPointF(frame.left(), frame.top() + 4.0));
	overhang.setColorAt(0.0, withAlpha(shadowInk, dark ? 165 : 120));
	overhang.setColorAt(1.0, withAlpha(shadowInk, 0));
	painter.fillRect(QRectF(frame.left(), frame.top(), frame.width(), 4.0), overhang);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setPen(QPen(withAlpha(lightInk, dark ? 30 : 140), 1));
	painter.drawLine(QPointF(frame.left() + 2.0, frame.bottom() - 0.5), QPointF(frame.right() - 2.0, frame.bottom() - 0.5));
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QRectF bank = frame.adjusted(2.0, 2.0, -2.0, -2.0);
	// The interlock groove only exists where the mechanism fits. A bank too
	// short for it keeps the keys and loses the slide, which is what a shallower
	// switch assembly actually looks like.
	const bool hasTrack = bank.height() >= 13.0;
	const qreal trackHeight = hasTrack ? qBound(3.0, qreal(qFloor(bank.height() * 0.24)), 5.0) : 0.0;
	const QRectF capBand(bank.left(), bank.top(), bank.width(),
		bank.height() - (hasTrack ? trackHeight + 1.0 : 0.0));
	const QRectF track(bank.left(), capBand.bottom() + 1.0, bank.width(), trackHeight);

	// A key's cap, cut out of the cell with a milled slot on each side so the
	// bank reads as separate keys rather than one divided strip.
	const auto capRect = [&](double index) {
		const QRectF cell = state.segmentRect(index);
		const qreal left = qMax(cell.left() + 1.0, bank.left());
		const qreal right = qMin(cell.right() - 1.0, bank.right());
		return QRectF(left, capBand.top(), qMax(4.0, right - left), capBand.height());
	};

	// One machined key. raised = the cap standing proud, with the work light on
	// its top chamfer; the other face is the latch-down face every switch on
	// this machine wears - the bevel inverted, shadow on top, the lit lip below.
	const auto paintCap = [&](const QRectF& cap, bool raised, uint seed) {
		QPainterPath capPath;
		capPath.addRoundedRect(cap, 2.0, 2.0);
		QLinearGradient face(cap.topLeft(), cap.bottomLeft());
		// The cream finish's plate colour is already at full value, so its metal
		// is stepped downward from the plate rather than upward - lighter() on it
		// would return the same colour and flatten the cap.
		if (raised)
		{
			face.setColorAt(0.0, dark ? panel.lighter(134) : panel);
			face.setColorAt(0.55, dark ? panel.lighter(114) : panel.darker(103));
			face.setColorAt(1.0, panel.darker(dark ? 112 : 110));
		}
		else
		{
			face.setColorAt(0.0, panel.darker(dark ? 146 : 124));
			face.setColorAt(0.45, panel.darker(dark ? 124 : 113));
			face.setColorAt(1.0, dark ? panel.lighter(118) : panel);
		}
		painter.setPen(Qt::NoPen);
		painter.fillPath(capPath, face);

		{
			QPainterStateGuard capState(&painter);
			painter.setClipPath(capPath);
			RackSkinDetail::paintGrain(painter, cap, grainInk, seed);
		}

		const QColor topEdge = raised ? withAlpha(lightInk, dark ? 46 : 155) : withAlpha(shadowInk, dark ? 175 : 125);
		const QColor bottomEdge = raised ? withAlpha(shadowInk, dark ? 165 : 95) : withAlpha(lightInk, dark ? 70 : 175);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setPen(QPen(topEdge, 1));
		painter.drawLine(QPointF(cap.left() + 1.5, cap.top() + 0.5), QPointF(cap.right() - 1.5, cap.top() + 0.5));
		painter.setPen(QPen(bottomEdge, 1));
		painter.drawLine(QPointF(cap.left() + 1.5, cap.bottom() - 0.5), QPointF(cap.right() - 1.5, cap.bottom() - 0.5));
		painter.setRenderHint(QPainter::Antialiasing, true);

		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(withAlpha(shadowInk, dark ? 200 : 115), 1));
		painter.drawPath(capPath);
	};

	QFont legendFont(tokens.fontFamily);
	legendFont.setPixelSize(10);
	legendFont.setBold(true);
	const QFontMetricsF legendMetrics(legendFont);

	for (int i = 0; i < state.labels.size(); i++)
	{
		const QRectF cap = capRect(i);
		const bool latched = i == state.selectedIndex;
		// A finger on a key always shows the pressed face, latched or not: the
		// momentary rule the sheet already gives the Device and Channel caps.
		const bool pressed = state.enabled && i == state.pressedIndex;
		const bool warmed = state.enabled && !latched && i == state.hoveredIndex;
		paintCap(cap, !(latched || pressed), uint(i) * 2654435761u + 17u);

		// Latched and warmed are mutually exclusive by construction, and both
		// are lamp light landing on metal rather than a fill standing in for a
		// state - the amber never becomes the cap's own colour.
		if (state.enabled && (latched || warmed))
		{
			QPainterPath capPath;
			capPath.addRoundedRect(cap, 2.0, 2.0);
			painter.setPen(Qt::NoPen);
			if (latched)
			{
				// The lamp under a latched cap bleeding into its recess. The
				// sunken face and the lit lamp are what say the key is in; the
				// warmth is only the light that reaches the metal.
				painter.fillPath(capPath, withAlpha(amber, dark ? 58 : 42));
				if (state.focused)
				{
					// Keyboard focus on the bank lands on the key it holds in:
					// that cap's own edge takes the focus colour, one line on
					// the outline the cap already has. Never a frame around the
					// bank (docs/skins/README.md, shared article 8).
					painter.setBrush(Qt::NoBrush);
					painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 220), 1));
					painter.drawPath(capPath);
				}
			}
			else
			{
				// Hover pre-heats the key: the face takes the first of the
				// lamp's heat and the hardware edge warms to amber, an edge no
				// other state draws. Painted is not visible, so neither of these
				// is a difference of a dozen alpha steps.
				painter.fillPath(capPath, withAlpha(amber, dark ? 38 : 34));
				painter.setBrush(Qt::NoBrush);
				painter.setPen(QPen(withAlpha(amber, dark ? 195 : 210), 1));
				painter.drawPath(capPath);
			}
		}

		// The key's own lamp, in the bezel-ring grammar every lamp on this
		// machine goes through (paintLed, restated here as monitorLamp) rather
		// than a bare glowing disc. Its PLACE on the cap is what makes this part
		// a selector key and not one of the four Phase 2 switches: Device
		// backlights the whole cap, Channel lights a window under it, Stage a
		// jewel on its top edge, and a selector key carries an inset panel LED
		// beside its legend, the way the picker's slots do.
		QRectF legendRect = cap;
		if (cap.width() >= 34.0)
		{
			const qreal lampRadius = 2.4;
			const QPointF lampCenter(cap.left() + 5.5 + lampRadius, cap.center().y());
			RackSkinDetail::paintLed(painter, lampCenter, lampRadius, amber, state.enabled && latched, dark);
			if (warmed)
			{
				// The picker's law: an unlit lamp pre-heats under the hand
				// instead of jumping straight to lit.
				painter.setPen(Qt::NoPen);
				painter.setBrush(withAlpha(amber, 95));
				painter.drawEllipse(lampCenter, lampRadius * 0.75, lampRadius * 0.75);
			}
			legendRect = cap.adjusted(lampRadius * 2.0 + 9.0, 0.0, -4.0, 0.0);
		}

		// The legend is cut into the cap, so it takes the offset contrast pass
		// the faceplate designations take - but these labels are translated UI
		// strings, not hardware printing, so they are engraved as written: no
		// uppercasing, no tracking (the plate's channel caption rule).
		painter.setFont(legendFont);
		QColor ink = latched ? bodyInk : mutedInk;
		if (warmed)
			ink = bodyInk;
		const QRectF engravedAt = (latched || pressed) ? legendRect.translated(0.0, 1.0) : legendRect;
		RackSkinDetail::engraveText(painter, engravedAt, Qt::AlignCenter,
			legendMetrics.elidedText(state.labels.at(i), Qt::ElideRight, qMax(0.0, legendRect.width())),
			ink, dark);
	}

	if (hasTrack)
	{
		// The interlock groove and the slide that runs in it. On a bank of
		// interlocked keys this is the part that genuinely moves: pressing a key
		// drives the slide sideways and the slide releases whichever key was
		// latched. It is the only continuous thing on the control, so it is the
		// only thing that reads selectionPosition.
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.fillRect(track, withAlpha(shadowInk, dark ? 185 : 125));
		painter.setPen(QPen(withAlpha(lightInk, dark ? 34 : 130), 1));
		painter.drawLine(QPointF(track.left(), track.bottom() - 0.5), QPointF(track.right(), track.bottom() - 0.5));
		painter.setRenderHint(QPainter::Antialiasing, true);

		const double travel = qBound(0.0, state.selectionPosition, double(state.labels.size() - 1));
		const QRectF travelling = state.segmentRect(travel);
		const qreal slideWidth = qMax(10.0, travelling.width() * 0.42);
		if (bank.width() > slideWidth + 2.0)
		{
			const qreal slideLeft = qBound(bank.left(), travelling.center().x() - slideWidth / 2.0,
				bank.right() - slideWidth);
			const QRectF slide(slideLeft, track.top() + 1.0, slideWidth, qMax(2.0, track.height() - 2.0));
			QLinearGradient steel(slide.topLeft(), slide.bottomLeft());
			steel.setColorAt(0.0, dark ? panel.lighter(215) : panel);
			steel.setColorAt(1.0, dark ? panel.lighter(130) : panel.darker(112));
			painter.setPen(Qt::NoPen);
			painter.setBrush(steel);
			painter.drawRoundedRect(slide, 1.0, 1.0);
			// The engagement finger, sitting under the key the slide holds in.
			painter.setPen(QPen(withAlpha(shadowInk, dark ? 175 : 125), 1));
			painter.drawLine(QPointF(slide.center().x(), slide.top() + 0.5),
				QPointF(slide.center().x(), slide.bottom() - 0.5));
		}
	}

	// Focus draws no frame around the bank. A focus indication only recolours
	// a border a part already has (maintainer ruling 2026-11-18): the frame
	// that used to sit inside the bezel made the recess, the keys, the hovered
	// key's amber edge and itself into nested boxes, and a click gives the
	// bank the focus, so it was up right after every use. The focus edge is
	// the latched cap's own outline, drawn with the keys above.

	if (!state.enabled)
	{
		// A powered-down unit: the lamps are out and a dark film covers the
		// bank, but the keys, the grain and the groove all stay. The equipment
		// is switched off, not taken away.
		painter.setPen(Qt::NoPen);
		painter.fillPath(well, withAlpha(shadowInk, dark ? 140 : 96));
	}

	wellState.restore();

	// The machined bezel around the opening.
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(withAlpha(shadowInk, dark ? 215 : 135), 1));
	painter.drawPath(well);
}
}

namespace
{
// The upright toggle's layout, shared by the painter and the size the skin
// asks for: the part in a column of its own, 6px of plate, then the two
// legends stacked left-aligned, one line pitch apart, the pair centred in
// the control's height and the whole assembly centred in its width.
constexpr qreal rackTogglePartWidth = 16.0;
constexpr qreal rackToggleGap = 6.0;
constexpr qreal rackToggleMargin = 2.0;

QFont rackToggleLegendFont(const QString& family)
{
	QFont font(family);
	font.setPixelSize(10);
	font.setBold(true);
	return font;
}

struct RackToggleLayout
{
	qreal lineHeight = 0.0;
	qreal partLeft = 0.0;
	qreal legendLeft = 0.0;
	qreal legendWidth = 0.0;
	qreal columnTop = 0.0;
	qreal lineCenter[2] = { 0.0, 0.0 };
};

RackToggleLayout rackToggleLayout(const QRectF& rect, const QStringList& labels, const QFont& legendFont)
{
	const QFontMetricsF metrics(legendFont);
	RackToggleLayout layout;
	layout.lineHeight = qCeil(metrics.height());
	qreal widest = 0.0;
	for (const QString& label : labels)
		widest = qMax(widest, metrics.horizontalAdvance(label));
	const qreal assembly = rackTogglePartWidth + rackToggleGap + qCeil(widest);
	layout.partLeft = qMax(rect.left() + rackToggleMargin, qreal(qFloor(rect.center().x() - assembly / 2.0 + 0.5)));
	layout.legendLeft = layout.partLeft + rackTogglePartWidth + rackToggleGap;
	layout.legendWidth = qMax(0.0, rect.right() + 1.0 - rackToggleMargin - layout.legendLeft);
	layout.columnTop = rect.top() + qMax(0.0, qreal(qFloor((rect.height() - 2.0 * layout.lineHeight) / 2.0)));
	layout.lineCenter[0] = layout.columnTop + layout.lineHeight / 2.0;
	layout.lineCenter[1] = layout.columnTop + layout.lineHeight * 1.5;
	return layout;
}
}

Qt::Orientation RackSkin::segmentedControlAxis(int optionCount) const
{
	// The toggle's two positions are stacked, so its options are too: the upper
	// half of the control answers for the upper word.
	return optionCount == 2 ? Qt::Vertical : Qt::Horizontal;
}

QSize RackSkin::segmentedControlSizeHint(const QStringList& labels, const QFont& font) const
{
	if (labels.size() != 2)
		return QSize();
	const QFontMetricsF metrics(rackToggleLegendFont(font.family()));
	qreal widest = 0.0;
	for (const QString& label : labels)
		widest = qMax(widest, metrics.horizontalAdvance(label));
	const int width = qCeil(rackToggleMargin + rackTogglePartWidth + rackToggleGap + widest + rackToggleMargin);
	// Never lower than the widget's own height, so no card changes height for
	// the toggle; the two legend lines fit in the 26px of the lowest one.
	const int height = qMax(26, qCeil(QFontMetricsF(font).height()) + 10);
	return QSize(width, height);
}

// Rack's pick-one control is hardware chosen by the count of options: a
// choice between two is an upright toggle switch with its two names printed
// beside it, a choice among three or more is a bank of interlocked keys
// (paintRackKeyBank above).
//
// The toggle is the lever of a small panel switch: the first option's name
// on the upper line, the second's on the lower, left-aligned in one column
// right beside the part, and the lever's tip standing at the height of the
// chosen name. The tip's two rests are the centres of the two legend lines,
// so the travel is one line pitch whatever the words are.
//
// Three verdicts of the maintainer (2026-11-26) shaped it:
// - first, the key bank the two-option controls used to wear (two machined
//   key caps with grain, a lamp in each, an interlock slide) was given far
//   too much force for choosing one of two: tone it down, keep it Rack, make
//   it read as a hardware part that selects one of two;
// - second, the slide switch that replaced it, a 36px part centred under the
//   gap between the legends, was ant-sized: after moving it there must be no
//   doubt which word it went to;
// - third, the slide switch whose knob rested under each word had a travel
//   as long as the words, and the wide space between its ends made one ask
//   whether it was right. The maintainer showed the toggles of a Topping
//   L30 II (a bat lever in an upright cutout, the position names in a column
//   beside it) as the idea, with the note that it needed a correction to be
//   Rack.
//
// The lever's place is the only thing that says which option is chosen
// (shared article 1: a state is said once), so both names print in the same
// ink at the same weight whatever the choice, as printing on a real panel
// does. No lamp, no fill behind the chosen name.
void RackSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	if (state.labels.isEmpty() || state.rect.width() < 10 || state.rect.height() < 8)
		return;
	if (state.labels.size() != 2)
	{
		paintRackKeyBank(painter, state, tokens);
		return;
	}

	const bool dark = skinIsDark(tokens);
	const QColor panel(tokens.card);
	const QColor bodyInk(tokens.text);
	const QColor mutedInk(tokens.mutedText);
	// Light and shadow instead of palette entries: a machined part is the one
	// work light falling across metal. The dark side keeps the cream finish warm
	// by mixing the plate's own ink toward black rather than laying a neutral
	// grey over it - this skin's shadows are never cold.
	const QColor shadowInk = dark ? RackPalette::shadow(255) : mixColor(bodyInk, RackPalette::shadow(255), 0.35);
	const QColor lightInk = RackPalette::light(255);
	// One light colour for the whole part, pointer and keyboard alike: bronze
	// on the dark finish (the colour every cap that answers the pointer or holds
	// focus wears there), the focus colour on the light one, as the VST fill
	// block's caps do. The part has no lamp, so amber has no place on it in the
	// dark.
	const QColor engagedEdge(dark ? RackSkinDetail::darkEngagedFrame() : tokens.focusRing);

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QFont legendFont = rackToggleLegendFont(tokens.fontFamily);
	const QFontMetricsF legendMetrics(legendFont);
	const RackToggleLayout layout = rackToggleLayout(QRectF(state.rect), state.labels, legendFont);

	// The legends: lettering printed on the plate, engraved as written (they
	// are translated UI strings, not hardware printing: no uppercasing, no
	// tracking, the plate's channel caption rule). A powered-down unit keeps
	// its printing, faded, as every print on this machine fades.
	// Which line an option prints on comes from the cell the widget tiles for
	// it (state.segmentRect): the first option on top, or on a rising scale
	// (SegmentedControl::setRisingScale) the higher option on top. The lines
	// keep one pitch however tall the control is.
	const auto lineOf = [&](int index) {
		return state.segmentRect(index).center().y() < QRectF(state.rect).center().y() ? 0 : 1;
	};
	const qreal rest[2] = { layout.lineCenter[lineOf(0)], layout.lineCenter[lineOf(1)] };
	painter.setFont(legendFont);
	const QColor legendInk = state.enabled ? bodyInk : withAlpha(mutedInk, 150);
	for (int i = 0; i < 2; i++)
	{
		const QRectF legendRect(layout.legendLeft, rest[i] - layout.lineHeight / 2.0,
			layout.legendWidth, layout.lineHeight);
		RackSkinDetail::engraveText(painter, legendRect, Qt::AlignLeft | Qt::AlignVCenter,
			legendMetrics.elidedText(state.labels.at(i), Qt::ElideRight, layout.legendWidth), legendInk, dark);
	}

	// The hand and the keyboard on the part: one line on an edge the part
	// already draws. The pointer over the option the lever is not at (pressing
	// there throws it) warms that edge at 150, a press there at 220, and
	// keyboard focus recolours it at 220 (shared article 8); while focus holds
	// it nothing else lights, so there is never a lit edge inside a lit edge
	// (the nested boxes of the 2026-11-18 ruling).
	const bool focusEdge = state.enabled && state.focused;
	const bool aimed = state.enabled && !focusEdge && state.hoveredIndex >= 0 && state.hoveredIndex != state.selectedIndex;
	const bool held = state.enabled && !focusEdge && state.pressedIndex >= 0 && state.pressedIndex != state.selectedIndex;
	QColor edge;
	if (focusEdge || held)
		edge = withAlpha(engagedEdge, 220);
	else if (aimed)
		edge = withAlpha(engagedEdge, 150);

	const qreal partCenterX = layout.partLeft + rackTogglePartWidth / 2.0;
	const qreal pivotY = (layout.lineCenter[0] + layout.lineCenter[1]) / 2.0;
	const double travel = qBound(0.0, state.selectionPosition, 1.0);
	const QPointF tip(partCenterX, rest[0] + (rest[1] - rest[0]) * travel);

	// The lever's metal: on the dark finish the plate mixed toward the warm
	// ivory ink, so it reads at a glance without turning cold; on the cream
	// finish the machined aluminium of the knobs. Powered down it loses its
	// light.
	QColor leverTop;
	QColor leverBottom;
	if (dark)
	{
		leverTop = mixColor(panel, bodyInk, state.enabled ? 0.74 : 0.28);
		leverBottom = mixColor(panel, bodyInk, state.enabled ? 0.44 : 0.18);
	}
	else
	{
		leverTop = state.enabled ? QColor(RackPalette::KnobAluminiumHighlight) : panel.darker(110);
		leverBottom = state.enabled ? QColor(RackPalette::KnobAluminiumEdge) : panel.darker(125);
	}

	// The part: the Topping's upright cutout corrected for Rack. The bare oval
	// hole with a chrome stick is what the maintainer found cheap here, so the
	// cutout wears this skin's recess grammar (a floor of the plate in shadow,
	// an overhang shadow under the top lip, an outline all round) inside a thin
	// dress bezel, and the lever is a machined stick from the pivot at the
	// cutout's middle to a rounded tip at the chosen end. The cutout is as tall
	// as the two legend lines, so each end stands beside its word. A studio
	// mini toggle (a round dress nut with a short bat) was drawn beside it and
	// judged at 100 %: its 13px nut with a foreshortened bat read as a blob,
	// and with the lower word chosen the ball and the nut merged, while the
	// cutout's tip stands plainly at the height of the chosen word.
	// Proportions from the photo: the cutout about twice the lettering's cap
	// height (about 7px here), the ball tip about 1.3 times it. The cutout is
	// 13px wide and 23px tall, its bezel 16x26, and the 9px tip rests on the
	// two line centres 13px apart, so the two rests never overlap. The odd
	// widths are centred on a whole pixel column (partCenterX), which puts
	// every outline on a half pixel.
	const qreal slotWidth = 13.0;
	const QRectF slot(partCenterX - slotWidth / 2.0, layout.columnTop + 1.5, slotWidth,
		layout.lineHeight * 2.0 - 3.0);
	const QRectF bezel = slot.adjusted(-1.5, -1.5, 1.5, 1.5);
	// The bezel ring is a plate tone lifted toward the ink, so on the dark
	// finish the resting part still shows its track: it is what says there are
	// two places. A neutral light, never bronze or amber, which stay for the
	// pointer and the focus.
	painter.setPen(Qt::NoPen);
	painter.setBrush(dark ? mixColor(panel, bodyInk, 0.34) : panel.darker(108));
	painter.drawRoundedRect(bezel, bezel.width() / 2.0, bezel.width() / 2.0);
	QPainterPath slotPath;
	slotPath.addRoundedRect(slot, slot.width() / 2.0, slot.width() / 2.0);
	painter.fillPath(slotPath, panel.darker(dark ? 185 : 140));
	{
		QPainterStateGuard slotState(&painter);
		painter.setClipPath(slotPath);
		QLinearGradient overhang(slot.topLeft(), QPointF(slot.left(), slot.top() + 4.0));
		overhang.setColorAt(0.0, withAlpha(shadowInk, dark ? 170 : 110));
		overhang.setColorAt(1.0, withAlpha(shadowInk, 0));
		painter.fillRect(QRectF(slot.left(), slot.top(), slot.width(), 4.0), overhang);
		// The work light caught on the lower lip: the cutout's own edge,
		// shifted a pixel up, lit along its lower part.
		painter.setClipRect(QRectF(slot.left(), slot.center().y() + 3.0, slot.width(), slot.height()),
			Qt::IntersectClip);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(withAlpha(lightInk, dark ? 70 : 170), 1.0));
		painter.drawPath(slotPath.translated(0.0, -1.0));
	}
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(withAlpha(shadowInk, dark ? 200 : 120), 1.0));
	painter.drawPath(slotPath);
	// The bezel's outline: the same colour all round (invariant 12), and the
	// one edge the pointer and the focus recolour.
	painter.setPen(QPen(edge.isValid() ? edge : withAlpha(shadowInk, dark ? 230 : 150), 1.0));
	painter.drawRoundedRect(bezel.adjusted(0.5, 0.5, -0.5, -0.5), bezel.width() / 2.0 - 0.5, bezel.width() / 2.0 - 0.5);

	const QPointF pivot(partCenterX, pivotY);
	// The lever's bar from the pivot to the tip, outlined so it holds against
	// the dark floor. The 9px tip covers most of it; what shows is the short
	// neck that makes the part read as a thrown lever rather than a slider.
	{
		QLinearGradient stickFace(QPointF(pivot.x() - 2.0, 0.0), QPointF(pivot.x() + 2.0, 0.0));
		stickFace.setColorAt(0.0, leverTop);
		stickFace.setColorAt(1.0, leverBottom);
		painter.setPen(QPen(withAlpha(shadowInk, dark ? 220 : 150), 1.0));
		painter.setBrush(stickFace);
		painter.drawRect(QRectF(pivot.x() - 2.0, qMin(pivot.y(), tip.y()), 4.0, qAbs(tip.y() - pivot.y())));
	}
	const qreal tipRadius = 4.5;
	QRadialGradient tipFace(tip - QPointF(0.8, 1.0), tipRadius * 1.6);
	tipFace.setColorAt(0.0, leverTop);
	tipFace.setColorAt(1.0, leverBottom);
	painter.setPen(QPen(withAlpha(shadowInk, dark ? 220 : 150), 1.0));
	painter.setBrush(tipFace);
	painter.drawEllipse(tip, tipRadius, tipRadius);
	if (state.enabled)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(lightInk, dark ? 120 : 220));
		painter.drawEllipse(tip - QPointF(1.3, 1.5), 1.2, 1.2);
	}
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
