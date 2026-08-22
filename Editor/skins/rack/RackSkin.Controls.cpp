/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
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
		glyphFont.setPixelSize(11);
		glyphFont.setBold(true);
		painter.setFont(glyphFont);
		const QPointF minusAt = pointAt(-0.07, scaleRadius - 2.5);
		const QPointF plusAt = pointAt(1.07, scaleRadius - 2.5);
		const QRectF minusRect(minusAt.x() - 7, minusAt.y() - 7, 14, 14);
		const QRectF plusRect(plusAt.x() - 7, plusAt.y() - 7, 14, 14);
		painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
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
		bodyGrad.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFF));
		bodyGrad.setColorAt(0.6, QColor(0xDE, 0xD7, 0xC6));
		bodyGrad.setColorAt(1.0, QColor(0xA8, 0x9F, 0x8C));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 200) : QColor(0x7E, 0x75, 0x62), 1));
	painter.setBrush(bodyGrad);
	painter.drawEllipse(center, bodyRadius, bodyRadius);

	// Machined cap step and the specular arc on its top edge.
	const qreal capRadius = bodyRadius - 3.5;
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 90 : 50), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, capRadius, capRadius);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 70 : 150), 1.2));
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
		pointerColor = dark ? QColor(0xF2, 0xEC, 0xDC) : QColor(0x2E, 0x29, 0x22);
	const QPointF pointerBase = pointAt(state.ratio, bodyRadius * 0.28);
	const QPointF pointerTip = pointAt(state.ratio, bodyRadius - 1.8);
	painter.setPen(QPen(QColor(0, 0, 0, state.enabled ? (dark ? 150 : 90) : 50), 3.6, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(pointerBase, pointerTip);
	painter.setPen(QPen(pointerColor, 2.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(pointerBase, pointerTip);

	// Hover/drag: the knob rim catches the light.
	if (state.enabled && (state.hovered || state.dragging))
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.accent), 90), 1.4));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(center, bodyRadius + 0.8, bodyRadius + 0.8);
	}

	// Keyboard focus: thin ring around the printed scale.
	if (state.focused)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 180), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(center, scaleRadius + 2.0, scaleRadius + 2.0);
	}

	// Powered-down knob: dim film over the body.
	if (!state.enabled)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(dark ? QColor(0, 0, 0, 90) : QColor(255, 252, 244, 130));
		painter.drawEllipse(center, bodyRadius, bodyRadius);
	}

	// No value window on the knob itself: a display pane across the cap is
	// not buildable hardware and it would cut the pointer line in half.
	// The value lives in the card's own LED
	// display (EditableValue) beside the knob; state.valueText is
	// deliberately unused here.
}

void RackSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
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
	const QColor shadowInk = dark ? QColor(0, 0, 0) : mixColor(bodyInk, QColor(0, 0, 0), 0.35);
	const QColor lightInk(255, 255, 255);
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
	legendFont.setPixelSize(9);
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

	if (state.focused)
	{
		// The machine's selection frame: a thin service line inside the bezel,
		// kept deliberately small - the keyboard ring is one of the two UI
		// necessities this constitution licenses on a hardware face.
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 215), 1));
		const qreal innerRadius = qMax(1.0, radius - 1.0);
		painter.drawRoundedRect(frame.adjusted(1.0, 1.0, -1.0, -1.0), innerRadius, innerRadius);
	}

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
	roleFont.setPixelSize(8);
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
	valueFont.setPixelSize(11);
	painter.setFont(valueFont);
	QColor print(state.enabled ? tokens.text : tokens.mutedText);
	QRectF printRect = cap.adjusted(6.0, drop, -5.0, drop);
	painter.setPen(print);
	painter.drawText(printRect, Qt::AlignLeft | Qt::AlignVCenter, state.layoutText);
	if (state.channelCount > 0)
	{
		const qreal valueWidth = QFontMetricsF(valueFont).horizontalAdvance(state.layoutText);
		QFont countFont(tokens.monoFontFamily);
		countFont.setPixelSize(8);
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
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200), 1.2, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(tail + QPointF(0, 1), head + QPointF(0, 1));
	painter.drawLine(head + QPointF(0, 1), head + QPointF(-3.0, -2.0));
	painter.drawLine(head + QPointF(0, 1), head + QPointF(-3.0, 4.0));
	painter.setPen(QPen(arrowInk, 1.2, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(tail, head);
	painter.drawLine(head, head + QPointF(-3.0, -3.0));
	painter.drawLine(head, head + QPointF(-3.0, 3.0));

	const bool pairVerdict = !state.verdictInputText.isEmpty() || !state.verdictOutputText.isEmpty();
	const bool hasText = pairVerdict || !state.verdictText.isEmpty();
	if (state.verdictRect.isEmpty()
		|| (!hasText && state.tone == VstBusFrameState::Tone::Neutral))
		return;

	QColor led(tokens.mutedText);
	bool lit = true;
	switch (state.tone)
	{
	case VstBusFrameState::Tone::Success: led = QColor(tokens.success); break;
	case VstBusFrameState::Tone::Warning: led = QColor(tokens.accent); break;
	case VstBusFrameState::Tone::Critical: led = QColor(tokens.danger); break;
	case VstBusFrameState::Tone::Neutral: lit = false; break;
	}
	if (!state.enabled)
		lit = false;

	const QPointF ledCenter(state.verdictRect.left() + 4.0, state.verdictRect.center().y() + 0.5);
	RackSkinDetail::paintLed(painter, ledCenter, 3.2, led, lit, dark);

	if (!hasText)
		return;

	QFont engraveFont(tokens.monoFontFamily);
	engraveFont.setPixelSize(9);
	engraveFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
	painter.setFont(engraveFont);
	const QColor textInk = state.tone == VstBusFrameState::Tone::Critical && state.enabled
		? QColor(tokens.danger) : withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);
	QRectF textRect(state.verdictRect);
	textRect.setLeft(ledCenter.x() + 9.0);
	QString text;
	if (pairVerdict)
		text = state.verdictInputText + QStringLiteral(" - ") + state.verdictOutputText;
	else
		text = state.verdictText;
	RackSkinDetail::engraveText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter,
		QFontMetricsF(engraveFont).elidedText(text, Qt::ElideRight, textRect.width()), textInk, dark);
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

	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(8);
	roleFont.setBold(true);
	roleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
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
	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(11);
	painter.setFont(valueFont);
	QColor print(state.silent || state.defaulted ? mutedInk : bodyInk);
	if (state.missingChannel)
		print = QColor(tokens.danger);
	if (!state.enabled)
		print = withAlpha(mutedInk, 150);
	painter.setPen(print);
	painter.drawText(cap.adjusted(6.0, drop, -5.0, drop),
		Qt::AlignLeft | Qt::AlignVCenter, state.valueText);
}

void RackSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);

	// The patch strip: a recessed band across the module face, engraved
	// edges in the same order every rack recess uses.
	const QRectF band(state.rect);
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlphaF(QColor(Qt::black), dark ? 0.16 : 0.05));
	painter.drawRect(band);
	painter.setPen(QPen(withAlphaF(QColor(Qt::black), dark ? 0.45 : 0.16), 1));
	painter.drawLine(band.topLeft() + QPointF(0, 0.5), band.topRight() + QPointF(0, 0.5));
	painter.setPen(QPen(withAlphaF(QColor(Qt::white), dark ? 0.07 : 0.5), 1));
	painter.drawLine(band.bottomLeft() - QPointF(0, 0.5), band.bottomRight() - QPointF(0, 0.5));

	if (state.latchRect.isNull())
		return;

	// The fold keeps the same modest cap as every selector on this unit -
	// one relief language (verdict r7) - and its state lives in the pilot
	// LED, which is why the cap needs no machined mount of its own: the
	// lamp says engaged, the latched-down face agrees.
	const QColor amber(tokens.accent);
	const QRectF cap = QRectF(state.latchRect).adjusted(0.5, 1.0, -0.5, -1.0);
	const bool down = !state.collapsed || state.latchPressed;
	paintRackBusCap(painter, cap, down, state.enabled, tokens, dark);
	if (state.enabled && (state.latchHovered || state.latchFocused))
	{
		painter.setPen(QPen(withAlpha(amber, state.latchFocused ? 220 : 140), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(cap, 2.0, 2.0);
	}
	const qreal drop = down ? 1.0 : 0.0;
	RackSkinDetail::paintLed(painter, QPointF(cap.left() + 8.0, cap.center().y() + 0.5 + drop), 2.6,
		amber, state.enabled && !state.collapsed, dark);
	QFont capFont(tokens.fontFamily);
	capFont.setPixelSize(8);
	capFont.setBold(true);
	capFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	painter.setFont(capFont);
	RackSkinDetail::engraveText(painter, cap.adjusted(14.0, drop, -3.0, drop),
		Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FILL"),
		withAlpha(QColor(state.collapsed ? tokens.mutedText : tokens.text), state.enabled ? 255 : 150), dark);
}
