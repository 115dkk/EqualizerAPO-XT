/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioSkin.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"

void StudioSkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	const bool lit = state.hovered || state.pressed;
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF frame = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);

	// Fill ladder: the disabled cards' dead-pane alpha at rest, rising
	// toward (but never reaching) the live cards' 0.88 when lit.
	const double fillAlpha = state.pressed ? 0.80 : (state.hovered ? 0.66 : 0.42);
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlpha(tokens.card, qRound(fillAlpha * 255)));
	painter.drawRoundedRect(frame, 8.0, 8.0);

	if (lit && !dark)
	{
		// The lit white slot cannot brighten, so a shade pooling at the
		// bottom edge carries the glass impression.
		QPainterPath panePath;
		panePath.addRoundedRect(frame.adjusted(1.0, 1.0, -1.0, -1.0), 7.0, 7.0);
		QLinearGradient depthShade(QPointF(frame.left(), frame.bottom() - frame.height() * 0.5), frame.bottomLeft());
		depthShade.setColorAt(0.0, QColor(24, 32, 51, 0));
		depthShade.setColorAt(1.0, QColor(24, 32, 51, state.pressed ? 34 : 26));
		painter.fillPath(panePath, depthShade);
	}

	// Base outline: a faint neutral hairline; keyboard focus wears the
	// neutral focus ring (the accent halo below stays a pointer answer).
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(state.focused ? QColor(tokens.focusRing) : withAlpha(tokens.border, lit ? 230 : 140), 1.0));
	painter.drawRoundedRect(frame, 8.0, 8.0);

	if (lit)
	{
		// Two border-hugging strokes fake the halo; press is one ladder
		// step up.
		painter.setPen(QPen(withAlpha(tokens.accent, state.pressed ? 170 : 120), 1.0));
		painter.drawRoundedRect(frame, 8.0, 8.0);
		painter.setPen(QPen(withAlpha(tokens.accent, state.pressed ? 80 : 48), 3.0));
		painter.drawRoundedRect(frame.adjusted(1.5, 1.5, -1.5, -1.5), 6.5, 6.5);

		if (dark)
		{
			// The centre-bright reflection lights on the fitted pane
			// (the card chrome's line, one step calmer).
			const double y = frame.top() + 1.5;
			QLinearGradient reflection(frame.left(), y, frame.right(), y);
			reflection.setColorAt(0.0, QColor(255, 255, 255, 0));
			reflection.setColorAt(0.5, QColor(255, 255, 255, state.pressed ? 96 : 72));
			reflection.setColorAt(1.0, QColor(255, 255, 255, 0));
			painter.setPen(QPen(QBrush(reflection), 1.0));
			painter.drawLine(QPointF(frame.left() + 7.0, y), QPointF(frame.right() - 7.0, y));
		}
	}

	// Caption: drawn plus + translated label, centred as one unit.
	QFont captionFont(tokens.fontFamily);
	captionFont.setPointSizeF(10.5);
	captionFont.setWeight(QFont::DemiBold);
	const QFontMetricsF metrics(captionFont);
	const double plusRadius = 4.0;
	const double gap = 8.0;
	const double textWidth = metrics.horizontalAdvance(state.label);
	const double totalWidth = plusRadius * 2.0 + gap + textWidth;
	const double left = frame.center().x() - totalWidth / 2.0;
	const QPointF plusCenter(left + plusRadius, frame.center().y());

	if (lit)
	{
		// The plus lights in the accent: bloom stroke first, core on top.
		painter.setPen(QPen(withAlpha(tokens.accent, state.pressed ? 96 : 70), 4.5, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(QPointF(plusCenter.x() - plusRadius, plusCenter.y()), QPointF(plusCenter.x() + plusRadius, plusCenter.y()));
		painter.drawLine(QPointF(plusCenter.x(), plusCenter.y() - plusRadius), QPointF(plusCenter.x(), plusCenter.y() + plusRadius));
		painter.setPen(QPen(QColor(tokens.accent), 1.6, Qt::SolidLine, Qt::RoundCap));
	}
	else
	{
		painter.setPen(QPen(withAlpha(tokens.mutedText, 200), 1.6, Qt::SolidLine, Qt::RoundCap));
	}
	painter.drawLine(QPointF(plusCenter.x() - plusRadius, plusCenter.y()), QPointF(plusCenter.x() + plusRadius, plusCenter.y()));
	painter.drawLine(QPointF(plusCenter.x(), plusCenter.y() - plusRadius), QPointF(plusCenter.x(), plusCenter.y() + plusRadius));

	painter.setFont(captionFont);
	painter.setPen(lit ? QColor(tokens.text) : QColor(tokens.mutedText));
	painter.drawText(QRectF(left + plusRadius * 2.0 + gap, frame.top(), textWidth + 4.0, frame.height()),
		Qt::AlignLeft | Qt::AlignVCenter, state.label);
}

void StudioSkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	if (!state.hovered && !state.pressed)
		return;

	painter.setRenderHint(QPainter::Antialiasing);
	const double y = rect.center().y() + 0.5;
	const double x0 = rect.left() + 2.0;
	const double x1 = rect.right() - 2.0;
	const bool pressed = state.pressed;

	const auto ray = [&](int accentAlpha, int violetAlpha) {
		QLinearGradient gradient(x0, y, x1, y);
		gradient.setColorAt(0.0, withAlpha(tokens.accent, 0));
		gradient.setColorAt(0.28, withAlpha(tokens.accent, accentAlpha));
		gradient.setColorAt(0.74, withAlpha(tokens.accent2, violetAlpha));
		gradient.setColorAt(1.0, withAlpha(tokens.accent2, 0));
		return gradient;
	};

	// Bloom, mid, core: the knob arc's stroke ladder laid flat.
	QPen bloomPen(QBrush(ray(pressed ? 96 : 64, pressed ? 74 : 48)), 5.0);
	bloomPen.setCapStyle(Qt::RoundCap);
	painter.setPen(bloomPen);
	painter.drawLine(QPointF(x0, y), QPointF(x1, y));
	QPen midPen(QBrush(ray(pressed ? 205 : 150, pressed ? 165 : 118)), 2.4);
	midPen.setCapStyle(Qt::RoundCap);
	painter.setPen(midPen);
	painter.drawLine(QPointF(x0, y), QPointF(x1, y));
	QPen corePen(QBrush(ray(255, 235)), 1.0);
	corePen.setCapStyle(Qt::RoundCap);
	painter.setPen(corePen);
	painter.drawLine(QPointF(x0, y), QPointF(x1, y));

	// Insertion point: the indicator dot (halo + core) on the ray's
	// accent stretch.
	const QPointF dot(rect.center().x(), y);
	painter.setPen(Qt::NoPen);
	painter.setBrush(withAlpha(tokens.accent, pressed ? 130 : 100));
	painter.drawEllipse(dot, 4.4, 4.4);
	painter.setBrush(QColor(tokens.accent));
	painter.drawEllipse(dot, 2.2, 2.2);
}
