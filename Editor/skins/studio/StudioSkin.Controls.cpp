/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioSkin.h"
#include "StudioBandColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"

using namespace StudioBandColors;

void StudioSkin::paintKnob(QPainter& painter, const QRect& rect, const KnobState& state, const SkinTokens& tokens) const
{
	painter.setRenderHint(QPainter::Antialiasing);

	// Centred square so the knob stays round in non-square hosts
	// (promoted legacy dials are 84x66, the card knob is 74x74).
	const QRectF inner = QRectF(rect).adjusted(9, 9, -9, -9);
	const double side = qMin(inner.width(), inner.height());
	const QRectF track(inner.center().x() - side / 2.0, inner.center().y() - side / 2.0, side, side);

	const double span = 270.0;
	const double start = 135.0;     // degrees clockwise from 3 o'clock
	const double ratio = qBound(0.0, state.ratio, 1.0);

	if (!state.enabled)
		painter.setOpacity(0.35);

	const QColor accent = studioBandPaintColor(painter, tokens);

	// Track: the full range geometry as a thin circle segment.
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QColor(tokens.border), 2.0, Qt::SolidLine, Qt::RoundCap));
	painter.drawArc(track, qRound(-start * 16), qRound(-span * 16));

	double arcFrom = start;
	double sweep = span * ratio;
	if (state.bipolar)
	{
		arcFrom = start + span / 2.0;  // 12 o'clock
		sweep = span * (ratio - 0.5);  // signed: cut grows left, boost right
	}

	// Luminance ladder: rest keeps a faint outer stroke so the arc glows
	// even untouched, hover blooms one full step and a drag turns the
	// light all the way up.
	const int halo = state.dragging ? 120 : (state.hovered ? 88 : 36);
	const struct { double width; int alpha; } layers[] = {
		{ 13.0, qMax(8, halo / 6) },
		{ 9.0, halo / 3 },
		{ 5.5, halo },
		{ 2.5, 255 }
	};
	for (const auto& layer : layers)
	{
		QColor stroke = accent;
		stroke.setAlpha(layer.alpha);
		painter.setPen(QPen(stroke, layer.width, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(track, qRound(-arcFrom * 16), qRound(-sweep * 16));
	}

	// 0 dB anchor: a luminous tick crossing the track at 12 o'clock,
	// drawn over the arc so the centre detent stays readable even at
	// small gains - bloom first, bright core on top. At 0 dB the
	// indicator dot sits right under it.
	if (state.bipolar)
	{
		const QPointF top(track.center().x(), track.top());
		QColor tickBloom = accent;
		tickBloom.setAlpha(110);
		painter.setPen(QPen(tickBloom, 3.5, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(top.x(), top.y() - 6.0), QPointF(top.x(), top.y() + 4.0));
		QColor tickCore(tokens.text);
		tickCore.setAlpha(235);
		painter.setPen(QPen(tickCore, 1.4, Qt::SolidLine, Qt::FlatCap));
		painter.drawLine(QPointF(top.x(), top.y() - 6.0), QPointF(top.x(), top.y() + 4.0));
	}

	// Indicator dot on the track at the arc end, with its own halo.
	const double endRadians = qDegreesToRadians(-(arcFrom + sweep));
	const QPointF dot(track.center().x() + qCos(endRadians) * side / 2.0,
		track.center().y() - qSin(endRadians) * side / 2.0);
	QColor dotHalo = accent;
	dotHalo.setAlpha(halo);
	painter.setPen(Qt::NoPen);
	painter.setBrush(dotHalo);
	painter.drawEllipse(dot, 6.0, 6.0);
	painter.setBrush(accent);
	painter.drawEllipse(dot, 3.0, 3.0);

	// Keyboard focus: a thin ring just outside the track.
	if (state.focused)
	{
		QColor ring = accent;
		ring.setAlpha(110);
		painter.setPen(QPen(ring, 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(track.adjusted(-4, -4, 4, 4));
	}

	// Numeric readout, mono, fading in on hover and solid while dragging.
	// Only painted when the host supplied a display string (promoted
	// legacy dials show their value in a separate spin box instead).
	if (!state.valueText.isEmpty() && state.enabled && (state.hovered || state.dragging))
	{
		QColor textColor(tokens.text);
		textColor.setAlpha(state.dragging ? 255 : 210);
		painter.setPen(textColor);
		QFont valueFont(tokens.monoFontFamily);
		valueFont.setPointSizeF(qMax(7.0, painter.font().pointSizeF() - 1.0));
		valueFont.setWeight(QFont::DemiBold);
		painter.setFont(valueFont);
		painter.drawText(rect, Qt::AlignCenter, state.valueText);
	}
}

void StudioSkin::paintSegmentedControl(QPainter& painter, const SegmentedControlState& state, const SkinTokens& tokens) const
{
	if (state.labels.isEmpty())
		return;

	const bool dark = skinIsDark(tokens);
	const bool lit = state.enabled;
	// The pane light, which follows the row's band colour when the control
	// is tagged (a BiQuad card) and stays the base accent otherwise.
	const QColor light = studioBandPaintColor(painter, tokens);
	const int pointed = state.pressedIndex >= 0 ? state.pressedIndex : state.hoveredIndex;

	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QRectF frame = QRectF(state.rect).adjusted(0.5, 0.5, -0.5, -0.5);
	QPainterPath channel;
	channel.addRoundedRect(frame, 8.0, 8.0);

	// The channel is a sunken strip. It deepens whatever is behind it
	// rather than painting a colour of its own, so the same control reads
	// on the analysis bar and on a card's glass. Dark sinks with a black
	// wash; in light the text ink's shade carries it, because white glass
	// cannot brighten (S2).
	painter.setPen(Qt::NoPen);
	painter.fillPath(channel, dark ? QColor(0, 0, 0, lit ? 92 : 62) : withAlpha(tokens.text, lit ? 20 : 12));

	QPainterStateGuard channelState(&painter);
	painter.setClipPath(channel);

	// The sunken tell: a dark line settling just inside the top edge, the
	// graph pane's inner shadow at strip scale. Straight, so AA is off.
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.fillRect(QRectF(frame.left() + 6.0, frame.top() + 1.0, frame.width() - 12.0, 1.0),
		dark ? QColor(0, 0, 0, 110) : withAlpha(tokens.text, 34));
	painter.setRenderHint(QPainter::Antialiasing, true);

	// Keyboard focus is the outline of the light, not of the shape: a
	// wide faint stroke hugging the channel from inside, under the cap.
	if (lit && state.focused)
	{
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(withAlpha(tokens.focusRing, 70), 3.0));
		painter.drawRoundedRect(frame.adjusted(1.5, 1.5, -1.5, -1.5), 6.5, 6.5);
	}

	// Light pooling under the cursor - the picker's answer to hover, on a
	// cell the mark has not reached. Radial, so the pool has no edge of
	// its own to be mistaken for a second selection; pressing turns it one
	// step up the ladder. Held above the picker's alphas because a strip
	// cell is a fraction of a picker row: at the picker's numbers this
	// pool would be painted and still not be seen.
	if (lit && pointed >= 0 && qAbs(pointed - state.selectionPosition) > 0.35)
	{
		const bool poolPressed = state.pressedIndex == pointed;
		const QRectF cell = state.segmentRect(pointed);
		QRadialGradient pool(cell.center(), qMax(cell.width(), cell.height()) * 0.62);
		pool.setColorAt(0.0, withAlpha(light, dark ? (poolPressed ? 70 : 44) : (poolPressed ? 56 : 34)));
		pool.setColorAt(1.0, withAlpha(light, 0));
		painter.fillRect(cell, pool);
	}

	// The cap reads selectionPosition, never selectedIndex: a quick run
	// through three choices has to be one mark crossing the strip. While
	// it travels its bloom widens and brightens - light in motion smears -
	// and settles back as it lands.
	const QRectF cap = state.segmentRect(state.selectionPosition).adjusted(2.0, 2.0, -2.0, -2.0);
	const double travel = qBound(0.0, qAbs(state.selectionPosition - qRound(state.selectionPosition)) * 2.0, 1.0);
	const bool capPointed = pointed == state.selectedIndex;
	const bool capPressed = state.pressedIndex == state.selectedIndex;
	// 6px: the single 8px round reduced by the 2px inset, the way the card
	// chrome's inner pane rounds 7 inside its 1px border. Concentric, not
	// a second radius language.
	const double capRadius = 6.0;

	if (lit)
	{
		// Bloom stroke, translucent fill, hairline: the CLIP chip's ladder
		// in the pane's light instead of danger.
		const int bloom = (capPressed ? 88 : (capPointed ? 64 : 44)) + qRound(30.0 * travel);
		const int fill = dark
			? (capPressed ? 62 : (capPointed ? 50 : 38))
			: (capPressed ? 44 : (capPointed ? 34 : 26));
		painter.setPen(QPen(withAlpha(light, qMin(255, bloom)), 3.0 + 2.5 * travel));
		painter.setBrush(withAlpha(light, fill));
		painter.drawRoundedRect(cap, capRadius, capRadius);
		painter.setPen(QPen(withAlpha(light, capPointed ? 190 : 150), 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(cap, capRadius, capRadius);

		if (dark)
		{
			// Centre-bright reflection under the cap's top edge: the glass
			// formula, so the cap is a lit pane and not a coloured tile.
			const double y = cap.top() + 1.5;
			QLinearGradient reflection(cap.left(), y, cap.right(), y);
			reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
			reflection.setColorAt(0.5, QColor(255, 255, 255, capPointed ? 84 : 56));
			reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.setPen(QPen(QBrush(reflection), 1.0));
			painter.drawLine(QPointF(cap.left() + 5.0, y), QPointF(cap.right() - 5.0, y));
		}
		else
		{
			// The lit white cap cannot brighten, so the shade pooling at
			// its bottom edge says it is a pane sitting in the channel.
			QPainterPath capPath;
			capPath.addRoundedRect(cap, capRadius, capRadius);
			QLinearGradient depthShade(QPointF(cap.left(), cap.bottom() - cap.height() * 0.5), cap.bottomLeft());
			depthShade.setColorAt(0.0, withAlpha(tokens.text, 0));
			depthShade.setColorAt(1.0, withAlpha(tokens.text, capPointed ? 34 : 26));
			painter.fillPath(capPath, depthShade);
		}
	}
	else
	{
		// Lights out, and not one pixel of accent survives. The choice is
		// still on record, as a single neutral alpha step - the disabled
		// engaged chip's precedent, not a greyed-out accent.
		painter.setPen(QPen(withAlpha(tokens.border, dark ? 120 : 150), 1.0));
		painter.setBrush(dark ? QColor(255, 255, 255, 16) : withAlpha(tokens.text, 14));
		painter.drawRoundedRect(cap, capRadius, capRadius);
	}

	channelState.restore();

	// The channel's edge: a hairline that lights to the focus ring when
	// the keyboard holds the control.
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(lit && state.focused ? QColor(tokens.focusRing) : withAlpha(tokens.border, lit ? 210 : 130), 1.0));
	painter.drawRoundedRect(frame, 8.0, 8.0);

	// Labels. Hierarchy is luminance first, weight second: a cell's ink is
	// mixed toward the pane light by how much of the cap has arrived on
	// it, so the light travels with the mark instead of jumping to it. The
	// chosen cell ends in the light's own ink over the translucent fill,
	// which is the lit glass chip - never inverted text on a solid block.
	QFont labelFont(tokens.fontFamily);
	labelFont.setPointSizeF(10.0);
	for (int i = 0; i < state.labels.size(); i++)
	{
		const QRectF cell = state.segmentRect(i);
		const double cover = qBound(0.0, 1.0 - qAbs(double(i) - state.selectionPosition), 1.0);
		QColor ink;
		if (!lit)
		{
			ink = withAlpha(tokens.mutedText, cover > 0.5 ? 170 : 110);
		}
		else
		{
			const QColor base = (i == pointed && cover < 0.5)
				? QColor(tokens.text) : withAlpha(tokens.mutedText, 235);
			ink = mixColor(base, light, cover);
			ink.setAlpha(qRound(base.alpha() + (255 - base.alpha()) * cover));
		}

		QFont cellFont = labelFont;
		cellFont.setWeight(cover > 0.5 ? QFont::DemiBold : QFont::Normal);
		const QFontMetricsF cellMetrics(cellFont);
		painter.setFont(cellFont);
		painter.setPen(ink);
		painter.drawText(cell, Qt::AlignCenter,
			cellMetrics.elidedText(state.labels.at(i), Qt::ElideRight, qMax(8.0, cell.width() - 6.0)));
	}
}

// The VST3 bus contract as two quiet glass strips inside the reference
// card's data window: the caption sits outside on the pane, the value lives
// behind glass (the glass formula: alpha single-colour fill + top-edge
// reflection + 8px). Emphasis is wattage, never a new colour - rest < hover
// < open - and disabled is lights-out: the reflection and the caret's
// accent go dark with the ink.
void StudioSkin::paintVstBusSelector(QPainter& painter, const VstBusSelectorState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QColor glassInk = dark ? QColor(Qt::white) : QColor(Qt::black);
	const QRectF rect(state.rect);

	// The caption on the pane, right-aligned against the glass.
	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(9);
	roleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
	const QString role = state.roleText.toUpper();
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(role);
	painter.setFont(roleFont);
	painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	painter.drawText(QRectF(rect.left(), rect.top(), roleWidth, rect.height()),
		Qt::AlignRight | Qt::AlignVCenter, role);

	const QRectF cell = QRectF(rect.left() + roleWidth + 6.0, rect.top(),
		rect.width() - roleWidth - 6.0, rect.height()).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = 8.0;

	const qreal fill = !state.enabled ? 0.02
		: (state.pressed || state.menuOpen ? 0.085 : (state.hovered ? 0.06 : 0.035));
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlphaF(glassInk, fill));
	painter.drawRoundedRect(cell, radius, radius);

	QColor border = withAlphaF(glassInk, state.enabled ? 0.09 : 0.05);
	if (state.enabled && (state.focused || state.menuOpen))
		border = QColor(tokens.accent);
	else if (state.enabled && state.hovered)
		border = withAlpha(QColor(tokens.accent), 140);
	painter.setPen(QPen(border, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(cell, radius, radius);

	if (state.enabled)
	{
		// The top-edge reflection, clipped to the glass.
		QPainterPath clip;
		clip.addRoundedRect(cell.adjusted(1.0, 1.0, -1.0, -1.0), radius - 1.0, radius - 1.0);
		painter.setClipPath(clip);
		painter.setPen(QPen(withAlphaF(glassInk, 0.07), 1));
		painter.drawLine(QPointF(cell.left() + radius, cell.top() + 1.0),
			QPointF(cell.right() - radius, cell.top() + 1.0));
		painter.setClipping(false);
	}

	// Value in the window's mono data ink; the fixed width whispers in
	// accent behind it, and the caret keeps the accent's low wattage.
	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(12);
	painter.setFont(valueFont);
	QColor valueInk(state.hovered || state.menuOpen ? tokens.text : tokens.mutedText);
	if (!state.enabled)
		valueInk = withAlpha(QColor(tokens.mutedText), 150);
	painter.setPen(valueInk);
	QRectF textRect = cell.adjusted(6.0, 0, -6.0, 0);
	painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.layoutText);

	const qreal caretWidth = 6.0;
	QColor caretInk = withAlpha(QColor(tokens.accent), state.enabled ? 130 : 0);
	if (!state.enabled)
		caretInk = withAlpha(QColor(tokens.mutedText), 110);
	if (state.channelCount > 0)
	{
		painter.setPen(withAlpha(QColor(tokens.accent), state.enabled ? 150 : 90));
		const qreal valueWidth = QFontMetricsF(valueFont).horizontalAdvance(state.layoutText);
		painter.drawText(QRectF(textRect.left() + valueWidth + 5.0, textRect.top(),
			textRect.width() - valueWidth - 5.0, textRect.height()),
			Qt::AlignLeft | Qt::AlignVCenter, QString::number(state.channelCount));
	}
	const QPointF caretCenter(textRect.right() - caretWidth / 2.0, cell.center().y());
	QPainterPath caret;
	caret.moveTo(caretCenter + QPointF(-caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(caretWidth / 2.0, -caretWidth / 4.0));
	caret.lineTo(caretCenter + QPointF(0.0, caretWidth / 2.0));
	caret.closeSubpath();
	painter.fillPath(caret, caretInk);
}

// The joint and verdict on the pane: a thin light trace carries the signal
// from IN to OUT, and the verdict is a lamp point whose glow is faked with
// stroke rings (no effects), followed by quiet mono data ink.
void StudioSkin::paintVstBusFrame(QPainter& painter, const VstBusFrameState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	QColor trace(tokens.mutedText);
	trace = withAlpha(trace, state.enabled ? 170 : 100);
	const qreal midY = state.jointRect.center().y() + 0.5;
	const QPointF tail(state.jointRect.left() + 3.0, midY);
	const QPointF head(state.jointRect.right() - 3.0, midY);
	painter.setPen(QPen(trace, 1.1, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(tail, head);
	painter.drawLine(head, head + QPointF(-3.5, -3.5));
	painter.drawLine(head, head + QPointF(-3.5, 3.5));

	const bool pairVerdict = !state.verdictInputText.isEmpty() || !state.verdictOutputText.isEmpty();
	const bool hasText = pairVerdict || !state.verdictText.isEmpty();
	if (state.verdictRect.isEmpty()
		|| (!hasText && state.tone == VstBusFrameState::Tone::Neutral))
		return;

	QColor lamp(tokens.mutedText);
	bool lit = true;
	switch (state.tone)
	{
	case VstBusFrameState::Tone::Success: lamp = QColor(tokens.success); break;
	case VstBusFrameState::Tone::Warning: lamp = QColor(tokens.warning); break;
	case VstBusFrameState::Tone::Critical: lamp = QColor(tokens.danger); break;
	case VstBusFrameState::Tone::Neutral: lit = false; break;
	}
	if (!state.enabled)
		lit = false;

	const QPointF lampCenter(state.verdictRect.left() + 3.0, state.verdictRect.center().y() + 0.5);
	if (lit)
	{
		// Faked glow: two stroke rings losing wattage outward.
		painter.setPen(QPen(withAlpha(lamp, 60), 3.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(lampCenter, 3.6, 3.6);
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(lit ? lamp : withAlpha(QColor(tokens.mutedText), 110));
	painter.drawEllipse(lampCenter, 2.4, 2.4);

	if (!hasText)
		return;

	QFont verdictFont(tokens.monoFontFamily);
	verdictFont.setPixelSize(10);
	painter.setFont(verdictFont);
	const QColor ink = state.tone == VstBusFrameState::Tone::Critical
		? lamp : withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150);
	painter.setPen(ink);
	QRectF textRect(state.verdictRect);
	textRect.setLeft(lampCenter.x() + 8.0);
	if (pairVerdict)
	{
		const QFontMetricsF metrics(verdictFont);
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, state.verdictInputText);
		const qreal markLeft = textRect.left() + metrics.horizontalAdvance(state.verdictInputText) + 3.0;
		const qreal markY = textRect.center().y() + 0.5;
		painter.setPen(QPen(ink, 1.0, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(markLeft, markY), QPointF(markLeft + 8.0, markY));
		painter.drawLine(QPointF(markLeft + 8.0, markY), QPointF(markLeft + 5.5, markY - 2.5));
		painter.drawLine(QPointF(markLeft + 8.0, markY), QPointF(markLeft + 5.5, markY + 2.5));
		painter.setPen(ink);
		QRectF outRect(textRect);
		outRect.setLeft(markLeft + 12.0);
		painter.drawText(outRect, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetricsF(verdictFont).elidedText(state.verdictOutputText, Qt::ElideRight, outRect.width()));
	}
	else
	{
		painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
			QFontMetricsF(verdictFont).elidedText(state.verdictText, Qt::ElideRight, textRect.width()));
	}
}

void StudioSkin::paintVstSlotFillCell(QPainter& painter, const VstSlotFillCellState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QColor glassInk = dark ? QColor(Qt::white) : QColor(Qt::black);
	const QRectF rect(state.rect);

	// The scribble-strip register, deliberately not the bus glass: the role
	// is the printed strip caption, the channel sits in a shallow inset
	// well. A channel pick must never read as a layout pick.
	QFont roleFont(tokens.fontFamily);
	roleFont.setPixelSize(9);
	const QString role = state.roleToken;
	const qreal roleWidth = QFontMetricsF(roleFont).horizontalAdvance(role);
	painter.setFont(roleFont);
	painter.setPen(withAlpha(QColor(tokens.mutedText), state.enabled ? 255 : 150));
	painter.drawText(QRectF(rect.left(), rect.top(), roleWidth, rect.height()),
		Qt::AlignRight | Qt::AlignVCenter, role);

	const QRectF well = QRectF(rect.left() + roleWidth + 5.0, rect.top() + 1.0,
		rect.width() - roleWidth - 5.0, rect.height() - 2.0).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlphaF(glassInk, state.pressed || state.menuOpen ? 0.10 : (state.hovered ? 0.07 : 0.045)));
	painter.drawRoundedRect(well, 4.0, 4.0);
	QColor border = withAlphaF(glassInk, 0.10);
	if (state.missingChannel)
		border = QColor(tokens.danger);
	else if (state.enabled && (state.focused || state.menuOpen))
		border = QColor(tokens.accent);
	painter.setPen(QPen(border, 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(well, 4.0, 4.0);

	QFont valueFont(tokens.monoFontFamily);
	valueFont.setPixelSize(11);
	painter.setFont(valueFont);
	QColor valueInk(state.silent || state.defaulted ? tokens.mutedText : tokens.text);
	if (state.missingChannel)
		valueInk = QColor(tokens.danger);
	if (!state.enabled)
		valueInk = withAlpha(QColor(tokens.mutedText), 150);
	painter.setPen(valueInk);
	painter.drawText(well.adjusted(6.0, 0, -12.0, 0), Qt::AlignLeft | Qt::AlignVCenter, state.valueText);

	const qreal caretHalf = 3.0;
	const QPointF caretMid(well.right() - 7.0, well.center().y() + 0.5);
	QPainterPath caret;
	caret.moveTo(caretMid + QPointF(-caretHalf, -caretHalf / 2.0));
	caret.lineTo(caretMid + QPointF(caretHalf, -caretHalf / 2.0));
	caret.lineTo(caretMid + QPointF(0.0, caretHalf));
	caret.closeSubpath();
	painter.fillPath(caret, withAlpha(QColor(tokens.accent), state.enabled ? 130 : 70));
}

void StudioSkin::paintVstSlotFillRail(QPainter& painter, const VstSlotFillRailState& state, const SkinTokens& tokens) const
{
	QPainterStateGuard guard(&painter);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const bool dark = skinIsDark(tokens);
	const QColor glassInk = dark ? QColor(Qt::white) : QColor(Qt::black);

	// A recessed console band between two hairlines: the rails belong to
	// the desk surface instead of floating over it.
	const QRectF band(state.rect);
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlphaF(glassInk, dark ? 0.035 : 0.05));
	painter.drawRect(band);
	painter.setPen(QPen(withAlphaF(glassInk, 0.08), 1));
	painter.drawLine(band.topLeft() + QPointF(0, 0.5), band.topRight() + QPointF(0, 0.5));
	painter.drawLine(band.bottomLeft() - QPointF(0, 0.5), band.bottomRight() - QPointF(0, 0.5));

	if (state.latchRect.isNull())
		return;
	// The fold is a lit console switch - cap plus lamp - never a dropdown.
	const QRectF cap = QRectF(state.latchRect).adjusted(0.5, 1.5, -0.5, -1.5);
	painter.setPen(QPen(state.latchFocused ? QColor(tokens.accent) : withAlphaF(glassInk, 0.14), 1));
	painter.setBrush(withAlphaF(glassInk, state.latchPressed ? 0.12 : (state.latchHovered ? 0.09 : 0.06)));
	painter.drawRoundedRect(cap, 4.0, 4.0);
	const QColor lamp = state.collapsed ? withAlpha(QColor(tokens.mutedText), 160) : QColor(tokens.accent);
	painter.setPen(Qt::NoPen);
	painter.setBrush(lamp);
	painter.drawEllipse(QPointF(cap.left() + 8.0, cap.center().y() + 0.5), 2.5, 2.5);
	QFont capFont(tokens.fontFamily);
	capFont.setPixelSize(9);
	capFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
	painter.setFont(capFont);
	painter.setPen(state.collapsed ? QColor(tokens.mutedText) : QColor(tokens.text));
	painter.drawText(cap.adjusted(14.0, 0, -4.0, 0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FILL"));
}
