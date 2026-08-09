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
	labelFont.setPointSizeF(9.0);
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
