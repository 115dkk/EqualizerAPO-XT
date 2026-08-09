/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Hardware Rack device selector: a patch bay. Constitution (the rack
	grammar and its laws): docs/skins/rack.md. Element mapping: the APO bus
	is a bus bar in a rail channel on the list's left edge; a device row is
	a 1U bay strip whose toggle is a 1/4" patch jack (checking seats a plug
	and patches a cable to the bus), with a panel lamp for the install
	state; sections are mounting rails with a DZUS quarter-turn fastener;
	the dialog buttons are machined caps; the troubleshooting disclosure is
	a service hatch.

	Paint only, no images; every colour derives from the tokens (black/white
	alpha passes are light and shadow, not palette). Engraved stencils are
	hardware printing (uppercase English, never translated); the device names
	and the localized status sentence are UI strings drawn as-is.
*/

#include "DeviceSkinPainter.h"

#include <QFont>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// Bay geometry: the rail channel on the list's left edge, the jack field
// that owns the toggle clicks, and the machined right ear.
const int kRailWidth = 13;
const int kToggleWidth = 60;
const int kEarWidth = 24;

// ── Shared metal recipes (token-derived, no palette entries) ────────────────

QColor metalHi(const SkinTokens& t, bool dark)
{
	return dark ? mixColor(QColor(t.card), QColor(t.text), 0.55) : QColor(255, 255, 255);
}

QColor metalMid(const SkinTokens& t, bool dark)
{
	return dark ? mixColor(QColor(t.card), QColor(t.text), 0.20) : mixColor(QColor(t.card), QColor(t.border), 0.55);
}

QColor metalLo(const SkinTokens& t, bool dark)
{
	return dark ? QColor(t.card).darker(165) : mixColor(QColor(t.border), QColor(t.mutedText), 0.55);
}

// Engraved faceplate printing: a contrast pass offset one pixel down (the
// recess edge catching the work light), then the body ink on top.
void engrave(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, bool dark)
{
	painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
	painter.drawText(rect.translated(0, 1), flags, text);
	painter.setPen(body);
	painter.drawText(rect, flags, text);
}

// Horizontal brushing grain (same grammar as RackChrome): per-line ink
// density varies deterministically with the seed, sparse polish lines mixed in.
void paintGrain(QPainter& painter, const QRectF& r, bool dark, uint seed, const SkinTokens& t)
{
	const int baseAlpha = dark ? 4 : 5;
	QColor ink = dark ? QColor(255, 255, 255) : mixColor(QColor(t.mutedText), QColor(t.text), 0.3);
	for (qreal y = r.top() + 2; y < r.bottom() - 1; y += 2)
	{
		const uint h = (seed ^ uint(qRound(y * 7.0))) * 2654435761u;
		const bool polish = (h >> 8) % 11u == 0;
		ink.setAlpha(baseAlpha + int(h % 7u) + (polish ? 6 : 0));
		painter.setPen(QPen(ink, 1));
		painter.drawLine(QPointF(r.left() + 2, y), QPointF(r.right() - 2, y));
	}
}

// A slotted machine screw; the slot angle varies per screw so a column of
// them never reads as a stamped texture.
void paintScrew(QPainter& painter, const QPointF& center, qreal radius, qreal slotDegrees, const SkinTokens& t, bool dark)
{
	QRadialGradient body(center - QPointF(radius * 0.35, radius * 0.35), radius * 1.8);
	body.setColorAt(0.0, metalHi(t, dark));
	body.setColorAt(0.45, metalMid(t, dark));
	body.setColorAt(1.0, metalLo(t, dark));
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 200) : withAlpha(metalLo(t, dark).darker(140), 220), 1));
	painter.setBrush(body);
	painter.drawEllipse(center, radius, radius);

	const qreal rad = qDegreesToRadians(slotDegrees);
	const QPointF dir(qCos(rad), qSin(rad));
	const QPointF a = center - dir * (radius - 1.1);
	const QPointF b = center + dir * (radius - 1.1);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 230 : 200), qMax(1.3, radius * 0.28), Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a, b);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 60 : 170), 0.8, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a + QPointF(0, 1), b + QPointF(0, 1));
}

// A panel LED lamp in a bezel ring. The glow is stacked alpha painting:
// three concentric low-alpha discs under the dome, no gradients.
void paintLamp(QPainter& painter, const QPointF& center, qreal radius, const QColor& litColor, bool lit, bool dark)
{
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 190) : QColor(0, 0, 0, 120), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.3, radius + 1.3);

	if (lit)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlpha(litColor, 26));
		painter.drawEllipse(center, radius * 3.4, radius * 3.4);
		painter.setBrush(withAlpha(litColor, 55));
		painter.drawEllipse(center, radius * 2.2, radius * 2.2);
		painter.setBrush(withAlpha(litColor, 95));
		painter.drawEllipse(center, radius * 1.45, radius * 1.45);
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

// A tiny wireframe stamp (the rack badge grammar: printed outline, no fill).
// Stylistic hardware printing - uppercase English only, never translated.
void paintStamp(QPainter& painter, const QPointF& topLeft, const QString& letters, const QColor& ink, bool dark)
{
	QFont stampFont(painter.font().family());
	stampFont.setPixelSize(7);
	stampFont.setBold(true);
	stampFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
	painter.setFont(stampFont);
	const qreal w = QFontMetricsF(stampFont).horizontalAdvance(letters) + 8;
	const QRectF box(topLeft.x(), topLeft.y(), w, 11);
	painter.setPen(QPen(withAlpha(ink, 130), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 1.5, 1.5);
	engrave(painter, box, Qt::AlignCenter, letters, withAlpha(ink, 210), dark);
}

// ── The patch bay parts ─────────────────────────────────────────────────────

// The APO bus: a dark rail channel (the rack interior has no finish) with a
// warm metal bus bar running its full height, continuous across the rows.
void paintBusRail(QPainter& painter, const QRectF& rail, const SkinTokens& t, bool dark)
{
	const QColor channel = mixColor(QColor(t.background), QColor(0, 0, 0), dark ? 0.45 : 0.72);
	painter.setPen(Qt::NoPen);
	painter.fillRect(rail, channel);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 160 : 130), 1));
	painter.drawLine(QPointF(rail.right() - 0.5, rail.top()), QPointF(rail.right() - 0.5, rail.bottom()));
	painter.drawLine(QPointF(rail.left() + 0.5, rail.top()), QPointF(rail.left() + 0.5, rail.bottom()));

	// The bus bar: accent-warmed metal, lit along its left edge.
	const QColor barTone = mixColor(QColor(t.mutedText), QColor(t.accent), 0.35);
	const QRectF bar(rail.center().x() - 2.0, rail.top(), 4.0, rail.height());
	QLinearGradient barGrad(bar.topLeft(), bar.topRight());
	barGrad.setColorAt(0.0, barTone.lighter(dark ? 150 : 120));
	barGrad.setColorAt(0.45, barTone);
	barGrad.setColorAt(1.0, barTone.darker(165));
	painter.fillRect(bar, barGrad);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 46 : 90), 1));
	painter.drawLine(QPointF(bar.left() + 0.5, bar.top()), QPointF(bar.left() + 0.5, bar.bottom()));
}

// The 1/4" patch jack: hex nut, washer ring and a dark bore whose lower rim
// catches the work light (recessed - the chamfer law's inverse).
void paintJack(QPainter& painter, const QPointF& c, const SkinTokens& t, bool dark)
{
	// Mounting recess shadow behind the nut.
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(0, 0, 0, dark ? 90 : 50));
	painter.drawEllipse(c, 12.4, 12.4);

	// Hex nut, flat-topped - machine-set, not hand-rotated.
	QPolygonF nut;
	for (int i = 0; i < 6; i++)
		nut << skinArcPoint(c, 11.2, 30.0 + 60.0 * i);
	QLinearGradient nutGrad(c.x(), c.y() - 11.2, c.x(), c.y() + 11.2);
	nutGrad.setColorAt(0.0, metalHi(t, dark));
	nutGrad.setColorAt(0.55, metalMid(t, dark));
	nutGrad.setColorAt(1.0, metalLo(t, dark));
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 210) : withAlpha(metalLo(t, dark).darker(150), 220), 1));
	painter.setBrush(nutGrad);
	painter.drawPolygon(nut);

	// Washer ring inside the nut.
	QRadialGradient washer(c - QPointF(2.2, 2.2), 13.0);
	washer.setColorAt(0.0, metalHi(t, dark));
	washer.setColorAt(0.65, metalMid(t, dark));
	washer.setColorAt(1.0, metalLo(t, dark));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 170 : 120), 1));
	painter.setBrush(washer);
	painter.drawEllipse(c, 8.2, 8.2);

	// The bore: a hole is dark in both finishes; its lower rim catches the
	// light.
	painter.setPen(QPen(QColor(0, 0, 0, 220), 1));
	painter.setBrush(withAlpha(QColor(0, 0, 0), 232));
	painter.drawEllipse(c, 5.0, 5.0);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 44 : 70), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawArc(QRectF(c.x() - 4.4, c.y() - 4.4, 8.8, 8.8), 210 * 16, 120 * 16);

	// Specular glint on the washer's upper-left.
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(255, 255, 255, dark ? 70 : 150));
	painter.drawEllipse(c + QPointF(-5.4, -5.8), 1.0, 1.0);
}

// A plug cap seated in (or approaching) the jack, with its cable stub. The
// cap is the plug's back end seen from the panel front; the cable leaves it
// toward the bus rail with a slight sag.
void paintPlug(QPainter& painter, const QPointF& capCenter, qreal capRadius, const QPointF& junction,
	const SkinTokens& t, bool dark, int alpha, bool sunk)
{
	// Cable first, so the cap covers its end. The rubber must read against
	// both the dark channel and the plate: charcoal a step lighter than the
	// dark finish, near-black on cream, with a top highlight keeping it
	// round.
	QPainterPath cable;
	cable.moveTo(capCenter.x() - capRadius * 0.4, capCenter.y());
	const qreal midX = (capCenter.x() + junction.x()) / 2.0;
	cable.cubicTo(QPointF(midX, capCenter.y() + 6.5), QPointF(midX, junction.y() + 6.5), junction);
	const QColor rubber = dark ? QColor(t.card).lighter(170)
		: mixColor(QColor(t.background), QColor(0, 0, 0), 0.62);
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(withAlpha(QColor(0, 0, 0), 140 * alpha / 255), 5.2, Qt::SolidLine, Qt::RoundCap));
	painter.drawPath(cable.translated(0, 1.0));
	painter.setPen(QPen(withAlpha(rubber, alpha), 4.0, Qt::SolidLine, Qt::RoundCap));
	painter.drawPath(cable);
	painter.setPen(QPen(QColor(255, 255, 255, (dark ? 70 : 60) * alpha / 255), 1.2, Qt::SolidLine, Qt::RoundCap));
	painter.drawPath(cable.translated(0, -1.2));

	// Strain relief collar where the cable meets the cap.
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlpha(rubber.lighter(dark ? 160 : 115), alpha));
	painter.drawRoundedRect(QRectF(capCenter.x() - capRadius - 3.2, capCenter.y() - 2.2, 5.0, 4.4), 1.6, 1.6);

	// The machined cap: bright turned metal, unmistakable against the empty
	// bores around it; sunk = pushed one step deeper (pressed feedback).
	const QColor capHi = dark ? mixColor(QColor(t.card), QColor(t.text), 0.85) : QColor(255, 255, 255);
	const QColor capMid = dark ? mixColor(QColor(t.card), QColor(t.text), 0.45) : metalMid(t, dark);
	const QColor capLo = dark ? mixColor(QColor(t.card), QColor(t.text), 0.18) : metalLo(t, dark);
	QRadialGradient capGrad(capCenter - QPointF(capRadius * 0.35, capRadius * 0.35), capRadius * 1.9);
	if (sunk)
	{
		capGrad.setColorAt(0.0, capMid);
		capGrad.setColorAt(1.0, capLo.darker(125));
	}
	else
	{
		capGrad.setColorAt(0.0, capHi);
		capGrad.setColorAt(0.5, capMid);
		capGrad.setColorAt(1.0, capLo);
	}
	painter.setPen(QPen(withAlpha(QColor(0, 0, 0), 200 * alpha / 255), 1));
	painter.setBrush(capGrad);
	// setAlphaF on a gradient brush is not a thing; fake ghosting by alpha on
	// the strokes and an opacity pass on the fill.
	if (alpha < 255)
	{
		painter.save();
		painter.setOpacity(alpha / 255.0);
		painter.drawEllipse(capCenter, capRadius, capRadius);
		painter.restore();
	}
	else
	{
		painter.drawEllipse(capCenter, capRadius, capRadius);
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(255, 255, 255, (sunk ? 60 : 140) * alpha / 255));
	painter.drawEllipse(capCenter + QPointF(-capRadius * 0.35, -capRadius * 0.4), capRadius * 0.22, capRadius * 0.22);
}

// The lit junction node where a patched cable lands on the bus bar. Stacked
// alpha glow, like the lamps.
void paintJunction(QPainter& painter, const QPointF& c, const QColor& litColor, double glow)
{
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlpha(litColor, int(30 + 20 * glow)));
	painter.drawEllipse(c, 7.0, 7.0);
	painter.setBrush(withAlpha(litColor, int(70 + 30 * glow)));
	painter.drawEllipse(c, 4.4, 4.4);
	painter.setPen(QPen(QColor(0, 0, 0, 180), 1));
	painter.setBrush(litColor.lighter(135));
	painter.drawEllipse(c, 2.6, 2.6);
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(255, 255, 255, 160));
	painter.drawEllipse(c + QPointF(-0.8, -0.9), 0.7, 0.7);
}

// ── The painter ─────────────────────────────────────────────────────────────

class RackDeviceSkin : public DeviceSkinPainter
{
public:
	int rowHeight(const QFontMetrics& fm, bool section) const override
	{
		if (section)
			return fm.height() + 20;
		// Two engraved text lines and the jack's vertical extent.
		return qMax(fm.height() * 2 + 22, 50);
	}

	QRect toggleRect(const QRect& rowRect) const override
	{
		// The rail and the whole jack field belong to the toggle.
		return QRect(rowRect.left(), rowRect.top(), kToggleWidth, rowRect.height());
	}

	void paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		if (s.section)
			paintSectionRail(painter, rect, s, t);
		else
			paintBayStrip(painter, rect, s, t);

		painter.restore();
	}

	QSize buttonSizeHint(const QFontMetrics& fm, const QString& text) const override
	{
		return QSize(fm.horizontalAdvance(text) + 48, qMax(36, fm.height() + 18));
	}

	void paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const bool dark = skinIsDark(t);
		const QColor amber(t.accent);
		const QRectF r = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
		const qreal radius = 3.0;

		// The rack opening's dark seam around the cap.
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 180 : 90), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r, radius, radius);

		// The machined cap face: raised at rest, recessed when pressed (the
		// gradient inverts and the face drops a step).
		const QRectF face = r.adjusted(1, 1, -1, -1);
		QColor base(t.card);
		if (s.enabled && s.hover > 0.0)
			base = mixColor(base, QColor(t.cardHover), 0.55 * s.hover);
		QLinearGradient capGrad(face.topLeft(), face.bottomLeft());
		if (s.pressed)
		{
			capGrad.setColorAt(0.0, base.darker(dark ? 140 : 112));
			capGrad.setColorAt(1.0, base.lighter(dark ? 112 : 102));
		}
		else
		{
			capGrad.setColorAt(0.0, base.lighter(dark ? 138 : 104));
			capGrad.setColorAt(1.0, base.darker(dark ? 128 : 112));
		}
		painter.setPen(Qt::NoPen);
		painter.setBrush(capGrad);
		painter.drawRoundedRect(face, radius - 1, radius - 1);

		// Chamfer under the one work light; a pressed cap inverts it.
		const QColor litEdge(255, 255, 255, dark ? 44 : 150);
		const QColor shadowEdge(0, 0, 0, dark ? 130 : 60);
		painter.setPen(QPen(s.pressed ? shadowEdge : litEdge, 1));
		painter.drawLine(QPointF(face.left() + radius, face.top() + 0.5), QPointF(face.right() - radius, face.top() + 0.5));
		painter.setPen(QPen(s.pressed ? litEdge : shadowEdge, 1));
		painter.drawLine(QPointF(face.left() + radius, face.bottom() - 0.5), QPointF(face.right() - radius, face.bottom() - 0.5));

		// The primary cap carries an amber-backlit legend; hover feeds the
		// lamp (stacked alpha, never a fill swap). The secondary cap only
		// warms its edge.
		QColor ink = s.primary ? amber : QColor(t.text);
		if (s.primary && s.enabled)
		{
			const int bleed = int((s.pressed ? 80 : 22) + 58 * s.hover);
			QRectF lampRect = face.adjusted(4, 3, -4, -3);
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(amber, bleed / 3));
			painter.drawRoundedRect(lampRect, radius - 1, radius - 1);
			painter.setBrush(withAlpha(amber, bleed / 2));
			painter.drawRoundedRect(lampRect.adjusted(6, 3, -6, -3), radius - 1, radius - 1);
			painter.setPen(QPen(withAlpha(amber, int(90 + 130 * s.hover)), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(face, radius - 1, radius - 1);
			if (s.hover > 0.0 || s.pressed)
				ink = ink.lighter(dark ? int(100 + 30 * qMax(s.hover, s.pressed ? 1.0 : 0.0)) : 100);
		}
		else if (s.enabled && s.hover > 0.0)
		{
			painter.setPen(QPen(withAlpha(amber, int(120 * s.hover)), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(face, radius - 1, radius - 1);
		}

		// Powered-down cap: dim film, muted printing, no engraving contrast.
		if (!s.enabled)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(dark ? QColor(0, 0, 0, 80) : withAlpha(QColor(t.surface), 150));
			painter.drawRoundedRect(face, radius - 1, radius - 1);
			painter.setPen(withAlpha(QColor(t.mutedText), 150));
			painter.drawText(rect, Qt::AlignCenter, s.text);
			painter.restore();
			return;
		}

		// The legend, engraved; a pressed cap drops it one pixel with the
		// face.
		const QRectF legendRect = s.pressed ? QRectF(rect).translated(0, 1) : QRectF(rect);
		engrave(painter, legendRect, Qt::AlignCenter, s.text, ink, dark);

		// Keyboard focus: the thin amber service ring inside the cap.
		if (s.focused)
		{
			painter.setPen(QPen(withAlpha(QColor(t.focusRing), 180), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(face.adjusted(2, 2, -2, -2), radius - 1.5, radius - 1.5);
		}

		painter.restore();
	}

	void paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const bool dark = skinIsDark(t);
		const QColor amber(t.accent);
		const QRectF plate = QRectF(rect).adjusted(0.5, 2.5, -0.5, -2.5);
		const qreal radius = 2.0;

		// The service hatch cover: a brushed access plate set into the panel.
		QColor base = mixColor(QColor(t.surface), QColor(t.card), 0.5);
		if (s.hover > 0.0)
			base = mixColor(base, QColor(t.cardHover), 0.5 * s.hover);
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 70), 1));
		painter.setBrush(base);
		painter.drawRoundedRect(plate, radius, radius);
		painter.save();
		QPainterPath clip;
		clip.addRoundedRect(plate, radius, radius);
		painter.setClipPath(clip);
		paintGrain(painter, plate, dark, uint(qHash(QStringLiteral("service-hatch"))), t);
		painter.restore();
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 34 : 130), 1));
		painter.drawLine(QPointF(plate.left() + radius, plate.top() + 1.0), QPointF(plate.right() - radius, plate.top() + 1.0));
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 55), 1));
		painter.drawLine(QPointF(plate.left() + radius, plate.bottom() - 1.0), QPointF(plate.right() - radius, plate.bottom() - 1.0));

		// Corner screws at the hatch's right end (the latch holds the left).
		const uint seed = uint(qHash(QStringLiteral("hatch-screws")));
		paintScrew(painter, QPointF(plate.right() - 9, plate.top() + 7), 2.8, qreal(seed % 180u), t, dark);
		paintScrew(painter, QPointF(plate.right() - 9, plate.bottom() - 7), 2.8, qreal((seed + 73u) % 180u), t, dark);

		// The T-handle latch: a quarter turn opens the hatch. Hover pre-heats
		// the bezel and the handle amber; the open latch keeps a warm handle.
		const QPointF latch(plate.left() + 19, plate.center().y());
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 190 : 130), 1));
		painter.setBrush(QColor(0, 0, 0, dark ? 90 : 40));
		painter.drawEllipse(latch, 8.0, 8.0);
		if (s.hover > 0.0)
		{
			painter.setPen(QPen(withAlpha(amber, int(160 * s.hover)), 1.4));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(latch, 9.4, 9.4);
		}
		QColor handleTone = metalMid(t, dark).lighter(dark ? 150 : 104);
		handleTone = mixColor(handleTone, amber, (s.open ? 0.45 : 0.0) + 0.45 * s.hover);
		painter.save();
		painter.translate(latch);
		painter.rotate(s.open ? 90.0 : 0.0);
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 200 : 150), 1));
		painter.setBrush(handleTone);
		painter.drawRoundedRect(QRectF(-6.5, -1.9, 13.0, 3.8), 1.8, 1.8);
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 70 : 150), 0.8));
		painter.drawLine(QPointF(-5.2, -0.7), QPointF(5.2, -0.7));
		painter.restore();

		// Engraved title (a translated UI string, drawn as-is), warming with
		// the cursor.
		QFont titleFont = painter.font();
		titleFont.setBold(true);
		painter.setFont(titleFont);
		const QFontMetrics tfm(titleFont);
		const int textLeft = int(latch.x()) + 18;
		const int textRight = int(plate.right()) - 22;
		const QColor titleInk = mixColor(QColor(t.mutedText), QColor(t.text), (s.open ? 0.55 : 0.0) + 0.5 * s.hover);
		engrave(painter, QRectF(textLeft, plate.top(), textRight - textLeft, plate.height()),
			Qt::AlignLeft | Qt::AlignVCenter, tfm.elidedText(s.title, Qt::ElideRight, textRight - textLeft), titleInk, dark);

		// The open hatch leaks the service light along its bottom seam.
		if (s.open || s.hover > 0.0)
		{
			const int heat = int((s.open ? 120 : 0) + 90 * s.hover);
			painter.setPen(QPen(withAlpha(amber, qMin(heat, 200)), 1));
			painter.drawLine(QPointF(plate.left() + 8, plate.bottom() + 1.0), QPointF(plate.right() - 8, plate.bottom() + 1.0));
		}

		painter.restore();
	}

private:
	// A section header: one of the rack's horizontal mounting rails, with a
	// DZUS quarter-turn fastener showing whether the bay is folded open.
	static void paintSectionRail(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t)
	{
		const bool dark = skinIsDark(t);
		const QColor amber(t.accent);
		const QRectF r = QRectF(rect).adjusted(0.5, 2.5, -0.5, -1.5);

		// Rail steel: a shade deeper than the faceplates, brushed.
		QColor steel = mixColor(QColor(t.surface), QColor(t.background), 0.35);
		if (s.hover > 0.0)
			steel = mixColor(steel, QColor(t.cardHover), 0.4 * s.hover);
		painter.setPen(Qt::NoPen);
		painter.setBrush(steel);
		painter.drawRect(r);
		paintGrain(painter, r, dark, uint(qHash(s.connection)) ^ 0x5A5Au, t);
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 30 : 120), 1));
		painter.drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5));
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 70), 1));
		painter.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

		// The DZUS fastener: slot horizontal = bay latched shut, a quarter
		// turn to vertical = folded open. Hover pre-heats it amber.
		const QPointF dzus(r.left() + 16, r.center().y());
		if (s.hover > 0.0)
		{
			painter.setPen(QPen(withAlpha(amber, int(150 * s.hover)), 1.4));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(dzus, 7.6, 7.6);
		}
		paintScrew(painter, dzus, 5.6, s.expanded ? 90.0 : 0.0, t, dark);

		// Engraved rail title (localized UI data, engraved as-is).
		QFont titleFont = painter.font();
		titleFont.setBold(true);
		painter.setFont(titleFont);
		const QFontMetrics tfm(titleFont);
		const QColor titleInk = mixColor(QColor(t.mutedText), QColor(t.text), 0.55 + 0.45 * s.hover);

		// Right end: BAY numbering and the side stamp - hardware printing.
		QFont markFont(t.fontFamily);
		markFont.setPixelSize(8);
		markFont.setBold(true);
		markFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
		const QFontMetricsF mfm(markFont);
		const QString bay = QStringLiteral("BAY %1").arg(s.index + 1, 2, 10, QLatin1Char('0'));
		const QString side = s.input ? QStringLiteral("REC") : QStringLiteral("PLAY");
		const qreal bayWidth = mfm.horizontalAdvance(bay);
		const qreal stampWidth = QFontMetricsF(markFont).horizontalAdvance(side); // stamp font differs but close enough for reserve
		const qreal marksLeft = r.right() - bayWidth - stampWidth - 34;

		const int textLeft = int(dzus.x()) + 16;
		const int textRight = int(marksLeft) - 10;
		engrave(painter, QRectF(textLeft, r.top(), qMax(20, textRight - textLeft), r.height()),
			Qt::AlignLeft | Qt::AlignVCenter, tfm.elidedText(s.connection, Qt::ElideRight, qMax(20, textRight - textLeft)), titleInk, dark);

		// Mounting slots along the blank rail between title and printing.
		const qreal slotsFrom = textLeft + tfm.horizontalAdvance(tfm.elidedText(s.connection, Qt::ElideRight, qMax(20, textRight - textLeft))) + 18;
		painter.setPen(Qt::NoPen);
		for (qreal x = slotsFrom; x + 14 < marksLeft; x += 26)
		{
			painter.setBrush(QColor(0, 0, 0, dark ? 170 : 120));
			painter.drawRoundedRect(QRectF(x, r.center().y() - 1.6, 9.0, 3.2), 1.6, 1.6);
			painter.setPen(QPen(QColor(255, 255, 255, dark ? 30 : 90), 1));
			painter.drawLine(QPointF(x + 1, r.center().y() + 2.1), QPointF(x + 8, r.center().y() + 2.1));
			painter.setPen(Qt::NoPen);
		}

		painter.setFont(markFont);
		QColor markInk(t.mutedText);
		markInk.setAlpha(dark ? 170 : 200);
		engrave(painter, QRectF(marksLeft, r.top(), bayWidth + 4, r.height()),
			Qt::AlignLeft | Qt::AlignVCenter, bay, markInk, dark);
		paintStamp(painter, QPointF(marksLeft + bayWidth + 12, r.center().y() - 5.5), side, QColor(t.mutedText), dark);
	}

	// A device row: a 1U bay strip patched (or not) into the bus rail.
	static void paintBayStrip(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t)
	{
		// The widget font before any engraving pass swaps it out - it carries
		// the app point size, so Korean text keeps its metrics.
		const QFont baseFont = painter.font();
		const bool dark = skinIsDark(t);
		const QColor amber(t.accent);
		const QColor green(t.accent2);
		const bool pending = s.checked != s.installed;

		// The bus rail runs the full row height, continuous across the bay.
		const QRectF rail(rect.left(), rect.top(), kRailWidth, rect.height());
		paintBusRail(painter, rail, t, dark);

		// The faceplate strip, seated in the rack with a dark seam above and
		// below.
		const QRectF face(rect.left() + kRailWidth, rect.top() + 1, rect.width() - kRailWidth - 1, rect.height() - 2);
		const qreal radius = 2.0;
		QPainterPath plate;
		plate.addRoundedRect(face, radius, radius);

		QColor ground(t.card);
		if (s.selected)
			ground = mixColor(ground, QColor(t.cardSelected), 0.85);
		if (s.hover > 0.0)
			ground = mixColor(ground, QColor(t.cardHover), (s.selected ? 0.3 : 0.55) * s.hover);
		painter.setPen(Qt::NoPen);
		painter.setBrush(ground);
		painter.drawPath(plate);

		painter.save();
		painter.setClipPath(plate);

		// Brushed sheen: the rolled top edge falling into shadow, plus the
		// per-unit grain.
		QLinearGradient sheen(face.topLeft(), face.bottomLeft());
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
		painter.fillRect(face, sheen);
		paintGrain(painter, face, dark, uint(qHash(s.device)) ^ (uint(s.index) * 2654435761u), t);

		// Machined chamfer under the one work light: lit top, shadowed
		// bottom.
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 34 : 140), 1));
		painter.drawLine(QPointF(face.left() + radius, face.top() + 0.5), QPointF(face.right() - radius, face.top() + 0.5));
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 130 : 60), 1));
		painter.drawLine(QPointF(face.left() + radius, face.bottom() - 0.5), QPointF(face.right() - radius, face.bottom() - 0.5));

		// The machined right ear: groove, corner screws, engraved unit
		// number.
		const qreal earLeft = face.right() - kEarWidth;
		painter.fillRect(QRectF(earLeft, face.top(), kEarWidth, face.height()), QColor(0, 0, 0, dark ? 52 : 20));
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
		painter.drawLine(QPointF(earLeft - 0.5, face.top()), QPointF(earLeft - 0.5, face.bottom()));
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
		painter.drawLine(QPointF(earLeft + 0.5, face.top()), QPointF(earLeft + 0.5, face.bottom()));

		const uint seed = uint(qHash(s.device)) + uint(s.index) * 73u;
		const qreal earCx = earLeft + kEarWidth / 2.0;
		paintScrew(painter, QPointF(earCx, face.top() + 8), 3.1, qreal(seed % 180u), t, dark);
		paintScrew(painter, QPointF(earCx, face.bottom() - 8), 3.1, qreal((seed + 91u) % 180u), t, dark);

		QFont unitFont(t.monoFontFamily);
		unitFont.setPixelSize(9);
		unitFont.setBold(true);
		unitFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
		painter.setFont(unitFont);
		QColor unitInk(t.mutedText);
		unitInk.setAlpha(dark ? 170 : 200);
		engrave(painter, QRectF(earLeft, face.top(), kEarWidth, face.height()), Qt::AlignCenter,
			QStringLiteral("%1").arg(s.index + 1, 2, 10, QLatin1Char('0')), unitInk, dark);

		// Everything below may leave the plate (the patch cable crosses onto
		// the rail), so the clip ends here.
		painter.restore();

		// The status lamp: green = installed and staying, amber = pending
		// change (patching in or pulling out), dark = untouched. An
		// unavailable bay is powered down - the lamp is off.
		const QPointF lampAt(earLeft - 15, face.center().y());
		const QColor lampColor = pending ? amber : green;
		paintLamp(painter, lampAt, 3.2, lampColor, (pending || s.installed) && !s.unavailable, dark);

		// ── The jack field (the toggle) ──
		const QPointF jackC(rect.left() + kRailWidth + 24, face.center().y());
		paintJack(painter, jackC, t, dark);

		const QPointF junction(rail.center().x(), face.center().y());
		if (s.checked)
		{
			// A plug seated in the jack, its cable patched to the bus. The
			// junction lamp matches the status lamp's verdict; hover feeds
			// the glow, pressing pushes the plug a step deeper (the unplug
			// preview).
			paintPlug(painter, jackC, 5.8, junction, t, dark, 255, s.pressed);
			paintJunction(painter, junction, lampColor, s.hover);
			if (s.hover > 0.0 || s.pressed)
			{
				painter.setPen(QPen(withAlpha(amber, int(s.pressed ? 200 : 130 * s.hover)), 1.5));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(jackC, 9.4, 9.4);
			}
		}
		else
		{
			if (s.pressed)
			{
				// The plug pushed in: seated solid before the check lands.
				paintPlug(painter, jackC, 5.8, junction, t, dark, 255, false);
				painter.setPen(QPen(withAlpha(amber, 210), 1.5));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(jackC, 9.4, 9.4);
			}
			else if (s.hover > 0.0 && !s.unavailable)
			{
				// Pre-heat: the ring warms amber and a plug ghost approaches
				// from the bus side with the hover progress.
				painter.setPen(QPen(withAlpha(amber, int(160 * s.hover)), 1.5));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(jackC, 9.4, 9.4);
				const QPointF ghostC(junction.x() + 6 + (jackC.x() - junction.x() - 6) * s.hover, jackC.y());
				paintPlug(painter, ghostC, 5.4, junction, t, dark, int(40 + 130 * s.hover), false);
			}
		}

		// ── Text zones: engraved names line + the status sentence ──
		QFont nameFont = baseFont;
		nameFont.setFamily(t.fontFamily);
		nameFont.setBold(true);
		QFont statusFont = baseFont;
		statusFont.setFamily(t.fontFamily);
		statusFont.setBold(false);
		const QFontMetrics nfm(nameFont);
		const QFontMetrics sfm(statusFont);

		const int textLeft = rect.left() + kToggleWidth + 6;
		const int textRight = int(lampAt.x()) - 14;
		const int textWidth = qMax(20, textRight - textLeft);
		const int blockTop = rect.top() + (rect.height() - nfm.height() - sfm.height() - 3) / 2;

		const QColor nameInk = s.unavailable ? withAlpha(QColor(t.mutedText), 200) : QColor(t.text);
		painter.setFont(nameFont);
		const QString names = s.connection + QStringLiteral("  ·  ") + s.device;
		const QString elidedNames = nfm.elidedText(names, Qt::ElideRight, textWidth);
		engrave(painter, QRectF(textLeft, blockTop, textWidth, nfm.height()),
			Qt::AlignLeft | Qt::AlignVCenter, elidedNames, nameInk, dark);

		// Wireframe stamps after the name: printed outlines, not colour
		// pills (DEFAULT for the default endpoint, EXP for experimental).
		qreal stampX = textLeft + nfm.horizontalAdvance(elidedNames) + 10;
		const qreal stampY = blockTop + (nfm.height() - 11) / 2.0;
		if (s.defaultDevice && stampX + 52 < textRight)
		{
			paintStamp(painter, QPointF(stampX, stampY), QStringLiteral("DEFAULT"), QColor(t.mutedText), dark);
			stampX += QFontMetricsF(painter.font()).horizontalAdvance(QStringLiteral("DEFAULT")) + 20;
		}
		if (s.experimental && stampX + 30 < textRight)
			paintStamp(painter, QPointF(stampX, stampY), QStringLiteral("EXP"), QColor(t.mutedText), dark);

		QColor statusInk = s.unavailable ? withAlpha(QColor(t.mutedText), 150) : QColor(t.mutedText);
		if (pending && !s.unavailable)
			statusInk = amber;
		painter.setFont(statusFont);
		engrave(painter, QRectF(textLeft, blockTop + nfm.height() + 3, textWidth, sfm.height()),
			Qt::AlignLeft | Qt::AlignVCenter, sfm.elidedText(s.state, Qt::ElideRight, textWidth), statusInk, dark);

		// An unavailable endpoint is a powered-down unit: the lamp is off
		// (above) and a dim film covers the plate - the hardware stays.
		if (s.unavailable)
			painter.fillPath(plate, dark ? QColor(0, 0, 0, 80) : withAlpha(QColor(t.surface), 150));

		// Selection: the troubleshooting target wears the amber service
		// bezel over everything, film included.
		if (s.selected)
		{
			painter.setPen(QPen(withAlpha(amber, 200), 1));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(face.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
		}
	}
};
}

const DeviceSkinPainter* rackDeviceSkinPainter()
{
	static RackDeviceSkin painter;
	return &painter;
}
