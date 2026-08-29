/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab device selector: fear-free device cards. Constitution (pastel
	ladder, painted lift, stitch grammar): docs/skins/soft.md. Element
	mapping: devices are big rounded cards; the toggle is a large rounded
	check well whose outcome previews itself on hover; the status sentence
	leads with a small pastel state dot (green = already fine, blue = will
	install, amber = needs attention). Hover is a painted lift (one-step-
	darker plinth below + one-step-brighter face, never a blur).
	Unavailable devices sleep (dashed edge, sunk to the window ground,
	muted ink); the selected card keeps a calm pastel ring. Buttons are plump stadium pills on the
	ON-pastel ladder; the troubleshooting disclosure is a rounded tab
	borrowing the add-row grammar (a sunken chevron disc that flips ON
	when the panel opens).
*/

#include "DeviceSkinPainter.h"

#include <QFont>
#include <QPainterPath>

#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// The constitutional deep warm ink that rides every opaque pastel fill
// (same literal as the Editor's SoftSkin): white text on a pastel is
// exactly the low-contrast anxiety this skin removes.
QColor softWarmInk()
{
	return QColor(QStringLiteral("#2B251D"));
}

// The painted under-shadow: one value step below the ground the element
// floats over. Dark mode has a true darker step (the sunken surface);
// light mode's deepest calm neutral is the warm border.
QColor softPlinth(const SkinTokens& t)
{
	return skinIsDark(t) ? QColor(t.surfaceSunken) : QColor(t.border);
}

// One rung up the ON-pastel ladder (hover). The brighten target is the
// near-white token of the current mode, so both modes climb the same way.
QColor softBrighter(const QColor& pastel, const SkinTokens& t, double amount)
{
	const QColor target = skinIsDark(t) ? QColor(t.text) : QColor(t.surface);
	return mixColor(pastel, target, amount);
}

// One rung down the ladder (pressed): the pastel deepens toward the ink,
// the same step the Editor's ON pills take.
QColor softDeeper(const QColor& pastel, double amount)
{
	return mixColor(pastel, softWarmInk(), amount);
}

// The state dot's semantics (semantic colours as semantics only): green =
// already fine, blue = something will be installed, amber = needs a second
// look (an uninstall), muted =
// resting or asleep. All through the Soft pastel shelf so no dot is an
// alarm.
QColor stateDotColor(const DeviceRowState& s, const SkinTokens& t)
{
	const bool dark = skinIsDark(t);
	if (s.unavailable)
		return QColor(t.mutedText);
	if (s.checked && !s.installed)
		return softPastelize(QColor(t.accent), dark);
	if (!s.checked && s.installed)
		return softPastelize(QColor(t.warning), dark);
	if (s.checked && s.installed)
		return softPastelize(QColor(t.success), dark);
	return QColor(t.mutedText);
}

// The drawn check mark, sized relative to its well.
QPainterPath checkPath(const QRectF& box)
{
	QPainterPath path;
	path.moveTo(box.left() + box.width() * 0.28, box.top() + box.height() * 0.54);
	path.lineTo(box.left() + box.width() * 0.44, box.top() + box.height() * 0.70);
	path.lineTo(box.left() + box.width() * 0.74, box.top() + box.height() * 0.32);
	return path;
}

// Stroke pictograms in the picker tiles' friendly hand (round caps, no
// icon font): a speaker for the playback section, a microphone capsule for
// the capture section.
void drawSpeakerGlyph(QPainter& painter, const QPointF& c, qreal s, const QColor& ink)
{
	painter.setPen(QPen(ink, qMax<qreal>(1.4, s * 0.12), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	QPainterPath body;
	body.moveTo(c.x() - s * 0.50, c.y() - s * 0.22);
	body.lineTo(c.x() - s * 0.18, c.y() - s * 0.22);
	body.lineTo(c.x() + s * 0.12, c.y() - s * 0.50);
	body.lineTo(c.x() + s * 0.12, c.y() + s * 0.50);
	body.lineTo(c.x() - s * 0.18, c.y() + s * 0.22);
	body.lineTo(c.x() - s * 0.50, c.y() + s * 0.22);
	body.closeSubpath();
	painter.drawPath(body);
	const QRectF wave(c.x() + s * 0.22, c.y() - s * 0.34, s * 0.48, s * 0.68);
	painter.drawArc(wave, -60 * 16, 120 * 16);
}

void drawMicGlyph(QPainter& painter, const QPointF& c, qreal s, const QColor& ink)
{
	painter.setPen(QPen(ink, qMax<qreal>(1.4, s * 0.12), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	const QRectF capsule(c.x() - s * 0.15, c.y() - s * 0.50, s * 0.30, s * 0.60);
	painter.drawRoundedRect(capsule, s * 0.15, s * 0.15);
	const QRectF cradle(c.x() - s * 0.33, c.y() - s * 0.30, s * 0.66, s * 0.64);
	painter.drawArc(cradle, 180 * 16, -180 * 16);
	painter.drawLine(QPointF(c.x(), c.y() + s * 0.34), QPointF(c.x(), c.y() + s * 0.50));
}

// The fold chevron shared by the section pills and the disclosure tab:
// pointing right when the content is tucked away, down when it is out.
QPainterPath foldChevron(const QPointF& c, bool down)
{
	QPainterPath chevron;
	if (down)
	{
		chevron.moveTo(c.x() - 4.0, c.y() - 2.0);
		chevron.lineTo(c.x(), c.y() + 3.0);
		chevron.lineTo(c.x() + 4.0, c.y() - 2.0);
	}
	else
	{
		chevron.moveTo(c.x() - 2.0, c.y() - 4.0);
		chevron.lineTo(c.x() + 3.0, c.y());
		chevron.lineTo(c.x() - 2.0, c.y() + 4.0);
	}
	return chevron;
}

// A section header: the picker's pastel category pill promoted to the
// device list. The playback side tints with the accent pastel, the capture
// side with the accent2 pastel, and a full-pastel disc carries the side's
// pictogram (opaque pastel + warm ink, the tile grammar) so the two
// families tell apart at a glance. Folding is safe to press: the pill
// takes the same gentle lift as the cards.
void paintSectionPill(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t)
{
	const bool dark = skinIsDark(t);
	const QColor card(t.card);
	const QColor pastel = softPastelize(QColor(s.input ? t.accent2 : t.accent), dark);

	const qreal lift = 1.2 * s.hover;
	const QRectF pill = QRectF(rect).adjusted(2.5, 3.5, -2.5, -4.5).translated(0.0, -lift);
	const qreal radius = pill.height() / 2.0;

	if (s.hover > 0.0)
	{
		painter.setPen(Qt::NoPen);
		painter.setBrush(withAlphaF(softPlinth(t), 0.5 * s.hover));
		painter.drawRoundedRect(pill.translated(0.0, lift + 1.5), radius, radius);
	}

	painter.setPen(QPen(withAlpha(QColor(t.border), 140 + int(60 * s.hover)), 1.0));
	painter.setBrush(mixColor(pastel, card, 0.83 - 0.10 * s.hover));
	painter.drawRoundedRect(pill, radius, radius);

	// Concentric: disc radius = pill radius - inset.
	const qreal inset = 4.0;
	const qreal discD = pill.height() - inset * 2.0;
	const QRectF disc(pill.left() + inset, pill.top() + inset, discD, discD);
	painter.setPen(Qt::NoPen);
	painter.setBrush(pastel);
	painter.drawEllipse(disc);
	if (s.input)
		drawMicGlyph(painter, disc.center(), discD * 0.60, softWarmInk());
	else
		drawSpeakerGlyph(painter, disc.center(), discD * 0.60, softWarmInk());

	QFont titleFont(t.fontFamily);
	titleFont.setPointSizeF(9.5);
	titleFont.setWeight(QFont::DemiBold);
	painter.setFont(titleFont);
	const QFontMetrics titleFm(titleFont);
	const int textLeft = int(disc.right()) + 9;
	const int chevronRoom = 24;
	const int titleWidth = int(pill.right()) - textLeft - chevronRoom - 8;
	const QString title = titleFm.elidedText(s.connection, Qt::ElideRight, titleWidth);
	painter.setPen(QColor(t.text));
	painter.drawText(QRect(textLeft, int(pill.top()), titleWidth, int(pill.height())),
		Qt::AlignVCenter | Qt::AlignLeft, title);

	const QPointF cc(textLeft + titleFm.horizontalAdvance(title) + 13.0, pill.center().y());
	painter.setPen(QPen(mixColor(QColor(t.mutedText), QColor(t.text), 0.5 * s.hover), 1.6,
		Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(foldChevron(cc, s.expanded));
}
}

namespace
{
class SoftDeviceSkin : public DeviceSkinPainter
{
public:
	int rowHeight(const QFontMetrics& fm, bool section) const override
	{
		// The tallest rows of the five skins: two comfortable text lines,
		// card padding, and inter-card air. Heights derive from the metrics
		// so the Korean fallback face never clips.
		if (section)
			return fm.height() + 24;
		return fm.height() * 2 + 32;
	}

	QRect toggleRect(const QRect& rowRect) const override
	{
		// The whole left end of the card belongs to the check well.
		return QRect(rowRect.left(), rowRect.top(), 60, rowRect.height());
	}

	void paintRow(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		if (s.section)
			paintSectionPill(painter, rect, s, t);
		else
			paintDeviceCard(painter, rect, s, t);
		painter.restore();
	}

	QSize buttonSizeHint(const QFontMetrics& fm, const QString& text) const override
	{
		// Plump stadium pills; the height comes from the metrics so Korean
		// captions sit comfortably.
		return QSize(qMax(96, fm.horizontalAdvance(text) + 56), qMax(40, fm.height() + 22));
	}

	void paintButton(QPainter& painter, const QRect& rect, const DeviceButtonState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const qreal lift = s.enabled ? 1.0 * s.hover : 0.0;
		const QRectF pill = QRectF(rect).adjusted(3.5, 3.5, -3.5, -4.5).translated(0.0, -lift);
		const qreal radius = pill.height() / 2.0;

		if (lift > 0.0)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlphaF(softPlinth(t), 0.5 * s.hover));
			painter.drawRoundedRect(pill.translated(0.0, lift + 1.5), radius, radius);
		}

		QColor fill;
		QColor ink;
		QPen edge;
		if (!s.enabled)
		{
			// The sleeping triple: nothing to apply yet is a resting state,
			// never a forbidding one.
			fill = QColor(t.background);
			ink = withAlpha(QColor(t.mutedText), 170);
			edge = QPen(withAlpha(QColor(t.border), 170), 1.0, Qt::DashLine);
			edge.setCapStyle(Qt::RoundCap);
		}
		else if (s.primary)
		{
			// ON grammar: opaque accent pastel + deep warm ink, hovering one
			// rung up the pastel ladder and pressing one rung down.
			fill = QColor(t.accent);
			if (s.pressed)
				fill = softDeeper(fill, 0.16);
			else if (s.hover > 0.0)
				fill = softBrighter(fill, t, 0.16 * s.hover);
			ink = softWarmInk();
			edge = QPen(withAlpha(softDeeper(QColor(t.accent), 0.22), 150), 1.0);
		}
		else
		{
			fill = s.pressed ? QColor(t.surfaceSunken)
				: mixColor(QColor(t.card), QColor(t.cardHover), s.hover);
			ink = QColor(t.text);
			edge = QPen(withAlpha(QColor(t.border), 200), 1.0);
		}
		painter.setPen(edge);
		painter.setBrush(fill);
		painter.drawRoundedRect(pill, radius, radius);

		// Keyboard focus: the constitutional quiet halo, never a hard ring.
		if (s.focused && s.enabled)
		{
			painter.setPen(QPen(withAlpha(QColor(t.focusRing), 90), 3.0));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(pill.adjusted(-2.0, -2.0, 2.0, 2.0), radius + 2.0, radius + 2.0);
		}

		QFont buttonFont(t.fontFamily);
		buttonFont.setPointSizeF(9.5);
		buttonFont.setWeight(QFont::DemiBold);
		painter.setFont(buttonFont);
		painter.setPen(ink);
		painter.drawText(pill.toRect(), Qt::AlignCenter, s.text);
		painter.restore();
	}

	void paintDisclosure(QPainter& painter, const QRect& rect, const DeviceDisclosureState& s, const SkinTokens& t) const override
	{
		painter.save();
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);

		const QColor accent(t.accent);
		const QRectF strip = QRectF(rect).adjusted(0.5, 3.5, -0.5, -3.5);
		const qreal radius = strip.height() / 2.0;

		// An optional tab, not a dangerous one: hover fades in a soft pill
		// face one step above the tray; the open tab keeps a quiet pastel
		// wash so "these options are out" stays visible.
		if (s.hover > 0.0)
		{
			painter.setPen(QPen(withAlpha(QColor(t.border), int(150 * s.hover)), 1.0));
			painter.setBrush(withAlphaF(QColor(t.card), 0.9 * s.hover));
			painter.drawRoundedRect(strip, radius, radius);
		}
		if (s.open)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlpha(accent, 26));
			painter.drawRoundedRect(strip, radius, radius);
		}

		// The chevron disc borrows the add-row grammar: a quiet sunken disc
		// waiting at rest, warming toward the pastel on hover, and flipping
		// ON (opaque accent pastel + warm ink) while the panel is open.
		const qreal discD = qMin<qreal>(24.0, strip.height() - 8.0);
		const QRectF disc(strip.left() + 8.0, strip.center().y() - discD / 2.0, discD, discD);
		if (s.open)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(accent);
		}
		else
		{
			painter.setPen(QPen(mixColor(QColor(t.border), accent, 0.45 * s.hover), 1.0));
			painter.setBrush(mixColor(QColor(t.surfaceSunken), accent, 0.12 * s.hover));
		}
		painter.drawEllipse(disc);

		const QColor chevronInk = s.open ? softWarmInk()
			: mixColor(QColor(t.mutedText), QColor(t.text), 0.6 * s.hover);
		painter.setPen(QPen(chevronInk, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);
		painter.drawPath(foldChevron(disc.center(), s.open));

		QFont titleFont(t.fontFamily);
		titleFont.setPointSizeF(9.2);
		titleFont.setWeight(QFont::DemiBold);
		painter.setFont(titleFont);
		const QFontMetrics titleFm(titleFont);
		const int textLeft = int(disc.right()) + 10;
		const int titleWidth = rect.width() - (textLeft - rect.left()) - 10;
		painter.setPen(s.open ? QColor(t.text) : mixColor(QColor(t.mutedText), QColor(t.text), 0.65 * s.hover));
		painter.drawText(QRect(textLeft, rect.top(), titleWidth, rect.height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			titleFm.elidedText(s.title, Qt::ElideRight, titleWidth));
		painter.restore();
	}

private:
	// One device = one big friendly card: check well, two text zones (names
	// + the status sentence with its state dot), and the optional DEFAULT
	// chip. Hover is the gentle lift; sleep is the dashed/sunk/muted triple.
	static void paintDeviceCard(QPainter& painter, const QRect& rect, const DeviceRowState& s, const SkinTokens& t)
	{
		const bool dark = skinIsDark(t);
		const QColor warmInk = softWarmInk();
		const QColor bodyInk(t.text);
		const QColor muted(t.mutedText);
		const QColor accent(t.accent);

		// Asleep rows barely stir under the cursor.
		const double hover = s.unavailable ? s.hover * 0.4 : s.hover;
		const qreal lift = 1.5 * hover;
		const QRectF cardRect = QRectF(rect).adjusted(4.5, 3.5, -4.5, -5.5).translated(0.0, -lift);
		const qreal radius = 14.0;

		// The lift's under-shadow: a painted plinth one value step below the
		// ground (the two-step elevation rule; this skin never blurs).
		if (hover > 0.0)
		{
			painter.setPen(Qt::NoPen);
			painter.setBrush(withAlphaF(softPlinth(t), 0.55 * hover));
			painter.drawRoundedRect(cardRect.translated(0.0, lift + 1.5), radius, radius);
		}

		QColor face = s.selected ? QColor(t.cardSelected) : QColor(t.card);
		if (hover > 0.0)
			face = mixColor(face, QColor(t.cardHover), s.selected ? 0.35 * hover : hover);
		QPen edge(withAlpha(QColor(t.border), 170), 1.0);
		if (s.unavailable)
		{
			face = QColor(t.background);
			edge = QPen(withAlpha(QColor(t.border), 180), 1.0, Qt::DashLine);
			edge.setCapStyle(Qt::RoundCap);
		}
		painter.setPen(edge);
		painter.setBrush(face);
		painter.drawRoundedRect(cardRect, radius, radius);

		// Selected: the calm "we're talking about this one" pastel ring plus
		// its quiet halo (concentric: radius grows with the offset).
		if (s.selected)
		{
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(withAlpha(accent, 200), 1.5));
			painter.drawRoundedRect(cardRect, radius, radius);
			painter.setPen(QPen(withAlpha(accent, 70), 3.0));
			painter.drawRoundedRect(cardRect.adjusted(-2.5, -2.5, 2.5, 2.5), radius + 2.5, radius + 2.5);
		}

		// ---- The check well (the toggle) ----
		QRectF well(0.0, 0.0, 28.0, 28.0);
		well.moveCenter(QPointF(cardRect.left() + 27.0, cardRect.center().y()));

		painter.save();
		if (s.pressed)
		{
			painter.translate(well.center());
			painter.scale(0.94, 0.94);
			painter.translate(-well.center());
		}

		const QColor checkedFill = softPastelize(QColor(t.success), dark);
		QColor wellFill;
		QPen wellPen;
		QColor checkInk = warmInk;
		if (s.unavailable)
		{
			wellFill = s.checked ? mixColor(checkedFill, QColor(t.background), 0.62) : QColor(t.background);
			wellPen = QPen(withAlpha(muted, 130), 1.0, Qt::DashLine);
			wellPen.setCapStyle(Qt::RoundCap);
			checkInk = muted;
		}
		else if (s.checked)
		{
			// ON: opaque success pastel + the drawn warm-ink check. Pressing
			// steps one rung down the ladder, hovering one rung up.
			wellFill = checkedFill;
			if (s.pressed)
				wellFill = softDeeper(wellFill, 0.16);
			else if (hover > 0.0)
				wellFill = softBrighter(wellFill, t, 0.10 * hover);
			wellPen = QPen(withAlpha(softDeeper(checkedFill, 0.25), 200), 1.0);
		}
		else
		{
			// The light palette's sunken step is nearly the card colour, so
			// the empty well warms slightly toward the border to stay a
			// visible well; the dark step is already deep enough.
			const QColor restingWell = mixColor(QColor(t.surfaceSunken), QColor(t.border), dark ? 0.0 : 0.18);
			wellFill = s.pressed ? mixColor(restingWell, checkedFill, 0.35) : restingWell;
			wellPen = QPen(withAlpha(mixColor(QColor(t.border), accent, 0.40 * hover), dark ? 210 : 255), 1.0);
		}
		painter.setPen(wellPen);
		painter.setBrush(wellFill);
		painter.drawRoundedRect(well, 9.0, 9.0);

		const QPainterPath check = checkPath(well);
		painter.setBrush(Qt::NoBrush);
		if (s.checked)
		{
			painter.setPen(QPen(checkInk, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPath(check);
		}
		else if (s.pressed)
		{
			painter.setPen(QPen(withAlpha(warmInk, 170), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPath(check);
		}
		else if (hover > 0.0 && !s.unavailable)
		{
			// The preheat: the press's outcome previews itself as a ghost -
			// thinner and fainter than a real check so it never reads as one.
			painter.setPen(QPen(withAlpha(muted, int(90 * hover)), 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.drawPath(check);
		}
		painter.restore();

		// ---- Trailing DEFAULT chip (quiet pastel stadium) ----
		qreal rightLimit = cardRect.right() - 14.0;
		if (s.defaultDevice)
		{
			QFont chipFont(t.fontFamily);
			chipFont.setPointSizeF(6.8);
			chipFont.setWeight(QFont::DemiBold);
			chipFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
			const QFontMetrics chipFm(chipFont);
			const QString caption = QStringLiteral("DEFAULT");
			const qreal chipW = chipFm.horizontalAdvance(caption) + 16.0;
			const qreal chipH = chipFm.height() + 6.0;
			const QRectF chip(rightLimit - chipW, cardRect.center().y() - chipH / 2.0, chipW, chipH);
			QColor chipFill = mixColor(softPastelize(accent, dark), face, 0.70);
			if (s.unavailable)
				chipFill = mixColor(chipFill, QColor(t.background), 0.5);
			painter.setPen(Qt::NoPen);
			painter.setBrush(chipFill);
			painter.drawRoundedRect(chip, chipH / 2.0, chipH / 2.0);
			painter.setFont(chipFont);
			painter.setPen(s.unavailable ? withAlpha(muted, 170) : withAlpha(bodyInk, 220));
			painter.drawText(chip.toRect(), Qt::AlignCenter, caption);
			rightLimit = chip.left() - 10.0;
		}

		// ---- Two text zones ----
		QFont nameFont(t.fontFamily);
		nameFont.setPointSizeF(9.5);
		nameFont.setWeight(QFont::DemiBold);
		QFont deviceFont(t.fontFamily);
		deviceFont.setPointSizeF(9.0);
		QFont statusFont(t.fontFamily);
		statusFont.setPointSizeF(8.3);
		const QFontMetrics nameFm(nameFont);
		const QFontMetrics deviceFm(deviceFont);
		const QFontMetrics statusFm(statusFont);

		const qreal textLeft = well.right() + 13.0;
		const qreal textWidth = rightLimit - textLeft;
		if (textWidth > 24.0)
		{
			const int lineGap = 4;
			const int blockH = nameFm.height() + lineGap + statusFm.height();
			const qreal lineTop = cardRect.center().y() - blockH / 2.0;

			// Line 1: the connection leads, the device name follows in the
			// faded caption ink (the skin's two-line row identity).
			painter.setFont(nameFont);
			painter.setPen(s.unavailable ? muted : bodyInk);
			const QString connection = nameFm.elidedText(s.connection, Qt::ElideRight, int(textWidth));
			painter.drawText(QRectF(textLeft, lineTop, textWidth, nameFm.height()),
				Qt::AlignVCenter | Qt::AlignLeft, connection);
			const qreal deviceLeft = textLeft + nameFm.horizontalAdvance(connection) + 10.0;
			if (!s.device.isEmpty() && rightLimit - deviceLeft > 24.0)
			{
				painter.setFont(deviceFont);
				painter.setPen(withAlpha(muted, s.unavailable ? 150 : 220));
				painter.drawText(QRectF(deviceLeft, lineTop, rightLimit - deviceLeft, nameFm.height()),
					Qt::AlignVCenter | Qt::AlignLeft,
					deviceFm.elidedText(s.device, Qt::ElideRight, int(rightLimit - deviceLeft)));
			}

			// Line 2: the state dot narrates, then the localized sentence
			// says exactly what will happen.
			const qreal statusTop = lineTop + nameFm.height() + lineGap;
			const qreal statusCy = statusTop + statusFm.height() / 2.0;
			const QColor dot = stateDotColor(s, t);
			painter.setPen(Qt::NoPen);
			painter.setBrush(s.unavailable ? withAlpha(dot, 120) : dot);
			painter.drawEllipse(QPointF(textLeft + 3.5, statusCy), 3.5, 3.5);

			painter.setFont(statusFont);
			const bool pendingChange = s.checked != s.installed && !s.unavailable;
			painter.setPen(s.unavailable ? withAlpha(muted, 160)
				: (pendingChange ? mixColor(muted, bodyInk, 0.55) : muted));
			const qreal statusLeft = textLeft + 14.0;
			painter.drawText(QRectF(statusLeft, statusTop, rightLimit - statusLeft, statusFm.height()),
				Qt::AlignVCenter | Qt::AlignLeft,
				statusFm.elidedText(s.state, Qt::ElideRight, int(rightLimit - statusLeft)));
		}
	}
};
}

const DeviceSkinPainter* softDeviceSkinPainter()
{
	static SoftDeviceSkin painter;
	return &painter;
}
