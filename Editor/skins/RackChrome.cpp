/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	QPainter chrome for the "rack" skin. The tiebreaker for every stroke in
	this file is "would a hardware faceplate have it?" - screws, machined
	grooves, LEDs and engraved printing yes; glows, value arcs and abstract
	decoration no (the only exceptions are the thin keyboard-focus ring and
	the hover highlight, which are UI necessities kept deliberately small).
*/

#include "RackChrome.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "ISkin.h"

namespace
{
const int kEarWidth = 20;
const qreal kNameplateWidth = 78.0;
const qreal kNameplateHeight = 22.0;

bool isDarkPanel(const SkinTokens& tokens)
{
	return QColor(tokens.background).lightness() < 128;
}

QColor withAlpha(const QColor& color, int alpha)
{
	QColor result = color;
	result.setAlpha(alpha);
	return result;
}

// Engraved faceplate printing: a contrast pass offset one pixel down (the
// recess edge catching the light), then the body color on top.
void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, bool dark)
{
	painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
	painter.drawText(rect.translated(0, 1), flags, text);
	painter.setPen(body);
	painter.drawText(rect, flags, text);
}

// A slotted machine screw: radial-gradient steel body and a slot whose angle
// varies per screw so four of them never read as a stamped texture.
void paintScrew(QPainter& painter, const QPointF& center, qreal radius, qreal slotDegrees, bool dark)
{
	QRadialGradient body(center - QPointF(radius * 0.35, radius * 0.35), radius * 2.1);
	if (dark)
	{
		body.setColorAt(0.0, QColor(0x9A, 0xA4, 0xAC));
		body.setColorAt(0.55, QColor(0x4E, 0x57, 0x5E));
		body.setColorAt(1.0, QColor(0x23, 0x28, 0x2C));
	}
	else
	{
		body.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFC));
		body.setColorAt(0.55, QColor(0xC4, 0xBD, 0xAE));
		body.setColorAt(1.0, QColor(0x8E, 0x86, 0x76));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 200) : QColor(0x6B, 0x62, 0x52), 1));
	painter.setBrush(body);
	painter.drawEllipse(center, radius, radius);

	const qreal rad = qDegreesToRadians(slotDegrees);
	const QPointF dir(qCos(rad), qSin(rad));
	const QPointF a = center - dir * (radius - 1.2);
	const QPointF b = center + dir * (radius - 1.2);
	painter.setPen(QPen(dark ? QColor(10, 12, 14, 230) : QColor(60, 54, 44, 220), 1.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a, b);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 60 : 170), 0.8, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a + QPointF(0, 1), b + QPointF(0, 1));
}

// A panel LED in a bezel ring: lit = glowing dome with a specular dot,
// unlit = the same dome gone dark.
void paintLed(QPainter& painter, const QPointF& center, qreal radius, const QColor& litColor, bool lit, bool dark)
{
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 190) : QColor(70, 62, 50, 190), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.2, radius + 1.2);

	if (lit)
	{
		QRadialGradient halo(center, radius * 3.2);
		halo.setColorAt(0.0, withAlpha(litColor, 110));
		halo.setColorAt(1.0, withAlpha(litColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, radius * 3.2, radius * 3.2);
	}

	QRadialGradient dome(center - QPointF(radius * 0.3, radius * 0.3), radius * 1.6);
	if (lit)
	{
		dome.setColorAt(0.0, litColor.lighter(150));
		dome.setColorAt(1.0, litColor.darker(125));
	}
	else
	{
		const QColor off = litColor.darker(330);
		dome.setColorAt(0.0, off.lighter(140));
		dome.setColorAt(1.0, off);
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(dome);
	painter.drawEllipse(center, radius, radius);
	painter.setBrush(QColor(255, 255, 255, lit ? 170 : (dark ? 28 : 60)));
	painter.drawEllipse(center - QPointF(radius * 0.35, radius * 0.35), radius * 0.3, radius * 0.3);
}

// A 1/4" patchbay insert jack: steel flange around a dark sleeve hole.
void paintJack(QPainter& painter, const QPointF& center, bool dark)
{
	QRadialGradient flange(center - QPointF(1.4, 1.4), 7.5);
	if (dark)
	{
		flange.setColorAt(0.0, QColor(0xA8, 0xB1, 0xB8));
		flange.setColorAt(0.6, QColor(0x55, 0x5E, 0x64));
		flange.setColorAt(1.0, QColor(0x26, 0x2B, 0x2F));
	}
	else
	{
		flange.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFC));
		flange.setColorAt(0.6, QColor(0xC0, 0xB9, 0xAA));
		flange.setColorAt(1.0, QColor(0x86, 0x7E, 0x6E));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 210) : QColor(0x60, 0x58, 0x48), 1));
	painter.setBrush(flange);
	painter.drawEllipse(center, 4.6, 4.6);

	painter.setPen(QPen(QColor(0, 0, 0, 220), 1));
	painter.setBrush(QColor(8, 9, 10));
	painter.drawEllipse(center, 2.1, 2.1);

	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(255, 255, 255, dark ? 70 : 150));
	painter.drawEllipse(center + QPointF(-2.5, -2.7), 0.9, 0.9);
}

// Short engraved unit designation for the left rack ear.
QString unitLabel(const CommandRowInfo& info)
{
	static const struct { const char* type; const char* label; } table[] = {
		{ "biquad", "FILTER" },
		{ "graphiceq", "GRAPHIC" },
		{ "include", "PATCH" },
		{ "vst", "VST" },
		{ "copy", "ROUTE" },
		{ "preamp", "PREAMP" },
		{ "channel", "CHANNEL" },
		{ "device", "DEVICE" },
		{ "stage", "STAGE" },
		{ "delay", "DELAY" },
		{ "convolution", "CONV" },
		{ "loudness", "LOUDNESS" },
		{ "comment", "NOTE" },
		{ "text", "AUX" }
	};
	for (const auto& entry : table)
		if (info.type == QLatin1String(entry.type))
			return QLatin1String(entry.label);
	return info.command.toUpper().left(8);
}
}

namespace RackChrome
{
int earWidth()
{
	return kEarWidth;
}

int nameplateReserve()
{
	return int(kNameplateWidth) + 14;
}

void paintCardChrome(QPainter& painter, const QRect& rect, const CommandRowInfo& info, const SkinTokens& tokens)
{
	// Blank lines are gaps in the rack, not units: no faceplate.
	if (info.type == QLatin1String("spacer"))
		return;

	const bool dark = isDarkPanel(tokens);
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);

	// Stay inside the 1px QSS border (the machined plate edge) and clip all
	// painting to the plate so nothing bleeds past the rounded corners.
	const QRectF r = QRectF(rect).adjusted(1, 1, -1, -1);
	const qreal radius = qMax(2, tokens.borderRadius - 1);
	QPainterPath plate;
	plate.addRoundedRect(r, radius, radius);
	painter.setClipPath(plate);

	// Brushed-metal sheen: a bright rolled top edge falling into shadow.
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 22));
		sheen.setColorAt(0.12, QColor(255, 255, 255, 9));
		sheen.setColorAt(0.55, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 46));
	}
	else
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 110));
		sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 26));
	}
	painter.fillRect(r, sheen);

	// Per-type finish: Include units wear patchbay black, VST units a warm
	// charcoal; filters keep the bare aluminium.
	if (info.type == QLatin1String("include"))
		painter.fillRect(r, QColor(0, 0, 0, dark ? 64 : 28));
	else if (info.type == QLatin1String("vst"))
		painter.fillRect(r, dark ? QColor(34, 20, 6, 50) : QColor(74, 50, 14, 18));

	// Horizontal brushing texture.
	painter.setPen(QPen(dark ? QColor(255, 255, 255, 5) : QColor(96, 84, 64, 8), 1));
	for (qreal y = r.top() + 3; y < r.bottom() - 1; y += 3)
		painter.drawLine(QPointF(r.left() + 2, y), QPointF(r.right() - 2, y));

	// Rack ears, separated from the panel by a machined groove.
	const QRectF leftEar(r.left(), r.top(), kEarWidth, r.height());
	const QRectF rightEar(r.right() - kEarWidth, r.top(), kEarWidth, r.height());
	const QColor earFill(0, 0, 0, dark ? 52 : 20);
	painter.fillRect(leftEar, earFill);
	painter.fillRect(rightEar, earFill);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
	painter.drawLine(QPointF(leftEar.right(), r.top()), QPointF(leftEar.right(), r.bottom()));
	painter.drawLine(QPointF(rightEar.left(), r.top()), QPointF(rightEar.left(), r.bottom()));
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
	painter.drawLine(QPointF(leftEar.right() + 1, r.top()), QPointF(leftEar.right() + 1, r.bottom()));
	painter.drawLine(QPointF(rightEar.left() + 1, r.top()), QPointF(rightEar.left() + 1, r.bottom()));

	// Bezel edges: lit on top, shadowed at the bottom.
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left() + radius, r.top() + 0.5), QPointF(r.right() - radius, r.top() + 0.5));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 140 : 70), 1));
	painter.drawLine(QPointF(r.left() + radius, r.bottom() - 0.5), QPointF(r.right() - radius, r.bottom() - 0.5));

	// Module groove under the control strip whenever the unit is opened (the
	// body below it then reads as controls mounted on the same module).
	if (r.height() >= tokens.rowHeight + 26)
	{
		const qreal y = r.top() + tokens.rowHeight;
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 110 : 55), 1));
		painter.drawLine(QPointF(leftEar.right() + 2, y), QPointF(rightEar.left() - 2, y));
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 24 : 110), 1));
		painter.drawLine(QPointF(leftEar.right() + 2, y + 1), QPointF(rightEar.left() - 2, y + 1));
	}

	// Four corner screws (two on very low rows).
	const uint seed = uint(qHash(info.command));
	const QPointF screws[] = {
		QPointF(r.left() + 10, r.top() + 9),
		QPointF(r.right() - 10, r.top() + 9),
		QPointF(r.left() + 10, r.bottom() - 9),
		QPointF(r.right() - 10, r.bottom() - 9)
	};
	const int screwCount = r.height() >= 40 ? 4 : 2;
	for (int i = 0; i < screwCount; i++)
		paintScrew(painter, screws[i], 4.0, qreal((seed + uint(i) * 73u) % 180u), dark);

	// Status LEDs on the left ear: green power LED (lit = line active), amber
	// SELECT LED below it.
	paintLed(painter, QPointF(r.left() + 10, r.top() + 21), 3.0, QColor(tokens.accent2), info.enabled, dark);
	if (r.height() >= 44)
		paintLed(painter, QPointF(r.left() + 10, r.top() + 31.5), 2.4, QColor(tokens.accent), info.selected, dark);

	// Include: patchbay insert jacks on the right ear.
	if (info.type == QLatin1String("include"))
	{
		paintJack(painter, QPointF(r.right() - 10, r.top() + 21), dark);
		if (r.height() >= 46)
			paintJack(painter, QPointF(r.right() - 10, r.top() + 32.5), dark);
	}

	// VST: riveted brass brand nameplate right of the control strip. The
	// header layout reserves this area (see RackSkin::prepareCommandRow).
	if (info.type == QLatin1String("vst") && r.width() >= 320)
	{
		const QRectF plateRect(rightEar.left() - 8 - kNameplateWidth,
			r.top() + (tokens.rowHeight - kNameplateHeight) / 2.0, kNameplateWidth, kNameplateHeight);
		QLinearGradient brass(plateRect.topLeft(), plateRect.bottomLeft());
		if (dark)
		{
			brass.setColorAt(0.0, QColor(0xD6, 0xB2, 0x6A));
			brass.setColorAt(0.5, QColor(0xA8, 0x85, 0x46));
			brass.setColorAt(1.0, QColor(0x86, 0x67, 0x30));
		}
		else
		{
			brass.setColorAt(0.0, QColor(0xE8, 0xC8, 0x86));
			brass.setColorAt(0.5, QColor(0xC4, 0xA0, 0x5C));
			brass.setColorAt(1.0, QColor(0x9A, 0x7A, 0x3C));
		}
		painter.setPen(QPen(QColor(0x5A, 0x44, 0x16), 1));
		painter.setBrush(brass);
		painter.drawRoundedRect(plateRect, 3, 3);

		QFont plateFont(tokens.fontFamily);
		plateFont.setPixelSize(9);
		plateFont.setBold(true);
		plateFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
		painter.setFont(plateFont);
		// Engraved into the brass itself, so the passes are brass-tinted in
		// both modes rather than following the panel's engraving direction.
		painter.setPen(QColor(255, 240, 200, 160));
		painter.drawText(plateRect.translated(1, 1), Qt::AlignCenter, QStringLiteral("VST"));
		painter.setPen(QColor(0x3A, 0x2A, 0x0C));
		painter.drawText(plateRect, Qt::AlignCenter, QStringLiteral("VST"));

		painter.setPen(QPen(QColor(0x55, 0x40, 0x14), 0.8));
		painter.setBrush(QColor(0xE9, 0xD3, 0x9A));
		painter.drawEllipse(QPointF(plateRect.left() + 6, plateRect.center().y()), 1.6, 1.6);
		painter.drawEllipse(QPointF(plateRect.right() - 6, plateRect.center().y()), 1.6, 1.6);
	}

	// Engraved unit designation running up the left ear on tall units.
	const QString label = unitLabel(info);
	if (!label.isEmpty() && r.height() >= 96)
	{
		QFont earFont(tokens.fontFamily);
		earFont.setPixelSize(8);
		earFont.setBold(true);
		earFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
		painter.save();
		painter.translate(r.left() + 14.5, r.bottom() - 16);
		painter.rotate(-90);
		painter.setFont(earFont);
		const QRectF textRect(0, -10, r.height() - 64, 20);
		engraveText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, label, withAlpha(QColor(tokens.mutedText), 200), dark);
		painter.restore();
	}

	// Commented-out line: the whole unit is powered down behind a dim film.
	if (!info.enabled)
		painter.fillPath(plate, dark ? QColor(0, 0, 0, 80) : QColor(255, 252, 244, 120));

	// Keyboard focus: a thin amber line along the inner bezel (UI necessity,
	// kept as small as a hardware unit's rail light).
	if (info.focused)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 190), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1, radius - 1);
	}

	painter.restore();
}

void paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens)
{
	const bool dark = isDarkPanel(tokens);
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
	// carries the value, as on real hardware.
	const QColor inkBase(tokens.mutedText);
	const int inkAlpha = state.enabled ? 200 : 90;
	for (int i = 0; i <= 10; i++)
	{
		const qreal ratio = i / 10.0;
		const bool centerTick = state.bipolar && i == 5;
		const bool major = i == 0 || i == 10 || centerTick;
		// The bipolar neutral (0 dB at 12 o'clock) is a bold accent tick.
		QColor ink = centerTick ? QColor(tokens.accent) : inkBase;
		ink.setAlpha(inkAlpha);
		painter.setPen(QPen(ink, major ? 2.0 : 1.0, Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(pointAt(ratio, bodyRadius + 3.5), pointAt(ratio, major ? scaleRadius + 0.5 : scaleRadius - 1.5));
	}

	// Bipolar knobs print the cut/boost glyphs in the dead zone under the
	// scale ends, like a gain pot's faceplate. Unipolar knobs stay plain, so
	// the two kinds never look alike.
	if (state.bipolar)
	{
		QFont glyphFont(tokens.fontFamily);
		glyphFont.setPixelSize(9);
		glyphFont.setBold(true);
		painter.setFont(glyphFont);
		painter.setPen(withAlpha(inkBase, inkAlpha));
		const QPointF minusAt = pointAt(-0.07, scaleRadius - 2.0);
		const QPointF plusAt = pointAt(1.07, scaleRadius - 2.0);
		painter.drawText(QRectF(minusAt.x() - 6, minusAt.y() - 6, 12, 12), Qt::AlignCenter, QStringLiteral("-"));
		painter.drawText(QRectF(plusAt.x() - 6, plusAt.y() - 6, 12, 12), Qt::AlignCenter, QStringLiteral("+"));
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
	// hand is on the knob), disabled grays it out.
	QColor pointerColor;
	if (!state.enabled)
		pointerColor = withAlpha(inkBase, 130);
	else if (state.dragging || state.hovered)
		pointerColor = QColor(tokens.accent);
	else
		pointerColor = dark ? QColor(0xF2, 0xEC, 0xDC) : QColor(0x2E, 0x29, 0x22);
	painter.setPen(QPen(pointerColor, 2.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(pointAt(state.ratio, bodyRadius * 0.30), pointAt(state.ratio, bodyRadius - 3.0));

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

	// LED display window set into the knob cap; it brightens while the knob
	// is touched (hover/drag/focus). Promoted legacy dials pass an empty
	// valueText (their value lives in a spin box) - then no window is shown.
	if (!state.valueText.isEmpty())
	{
		QFont ledFont(tokens.monoFontFamily);
		ledFont.setPixelSize(9);
		ledFont.setBold(true);
		const QFontMetricsF metrics(ledFont);
		const qreal w = qMin(side - 4.0, metrics.horizontalAdvance(state.valueText) + 10.0);
		const QRectF window(center.x() - w / 2.0, center.y() - 7.0, w, 14.0);
		painter.setPen(QPen(QColor(0, 0, 0, 220), 1));
		painter.setBrush(QColor(10, 14, 11, 235));
		painter.drawRoundedRect(window, 2, 2);

		QColor ledInk = dark ? QColor(0x86, 0xF2, 0xBA) : QColor(0x3E, 0xD6, 0x8E);
		if (!state.enabled)
			ledInk = withAlpha(ledInk, 70);
		else if (!(state.hovered || state.dragging || state.focused))
			ledInk = withAlpha(ledInk, 175);
		painter.setFont(ledFont);
		painter.setPen(ledInk);
		painter.drawText(window, Qt::AlignCenter, state.valueText);
	}
}
}
