/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QPainter>
#include <QPainterPath>
#include <QPainterStateGuard>

#include "Editor/skins/shared/SkinPaint.h"
#include "RackSkinDetail.h"

void RackSkin::paintAddRow(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF r = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = qMax(2, tokens.borderRadius - 1);
	QPainterPath opening;
	opening.addRoundedRect(r, radius, radius);
	painter.setClipPath(opening);

	// The bay's blank panel is missing, so the opening shows the rack's
	// interior - dark in both modes (the inside of a rack has no finish).
	QLinearGradient interior(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		interior.setColorAt(0.0, QColor(0x03, 0x04, 0x05));
		interior.setColorAt(0.4, QColor(0x0A, 0x0C, 0x0E));
		interior.setColorAt(1.0, QColor(0x12, 0x15, 0x18));
	}
	else
	{
		interior.setColorAt(0.0, QColor(0x2E, 0x2A, 0x23));
		interior.setColorAt(0.4, QColor(0x42, 0x3D, 0x33));
		interior.setColorAt(1.0, QColor(0x52, 0x4B, 0x3F));
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(interior);
	painter.drawRoundedRect(r, radius, radius);

	// The rack's mounting rails run behind the ear zones, each with two
	// empty bolt holes waiting for a unit's ears. The rails are the frame's
	// steel, one step lighter than the interior darkness.
	const QRectF leftRail(r.left(), r.top(), RackSkinDetail::EarWidth, r.height());
	const QRectF rightRail(r.right() - RackSkinDetail::EarWidth, r.top(), RackSkinDetail::EarWidth, r.height());
	const QColor railFill(255, 255, 255, dark ? 14 : 24);
	painter.fillRect(leftRail, railFill);
	painter.fillRect(rightRail, railFill);
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 130), 1));
	painter.drawLine(QPointF(leftRail.right(), r.top()), QPointF(leftRail.right(), r.bottom()));
	painter.drawLine(QPointF(rightRail.left(), r.top()), QPointF(rightRail.left(), r.bottom()));

	auto paintBoltHole = [&painter, dark](const QPointF& center) {
		// An empty threaded hole: a dark bore whose lower rim catches the
		// work light - recessed, so the light law is the plate chamfer's
		// inverse.
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 0, 0, dark ? 210 : 180));
		painter.drawEllipse(center, 2.6, 2.6);
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 40 : 70), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawArc(QRectF(center.x() - 2.6, center.y() - 2.6, 5.2, 5.2), 200 * 16, 140 * 16);
	};
	const qreal holeOffset = qMin(9.0, r.height() / 2.0 - 5.0);
	for (const QRectF& rail : { leftRail, rightRail })
	{
		paintBoltHole(QPointF(rail.center().x(), r.center().y() - holeOffset));
		paintBoltHole(QPointF(rail.center().x(), r.center().y() + holeOffset));
	}

	// The opening's chamfer is the faceplate's inverse: the top inner edge
	// falls into the overhang's shadow, the lower lip catches the work
	// light.
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 170 : 150), 1));
	painter.drawLine(QPointF(r.left() + radius, r.top() + 1.0), QPointF(r.right() - radius, r.top() + 1.0));
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 50), 1));
	painter.drawLine(QPointF(r.left() + radius, r.bottom() - 1.0), QPointF(r.right() - radius, r.bottom() - 1.0));

	// Stencilled marking inside the bay - hardware printing, never
	// translated (the tooltip carries the accessible caption). Always the
	// dark-recess engraving pass: the interior is dark in both finishes.
	const bool warm = state.hovered || state.pressed;
	QFont stencilFont(tokens.fontFamily);
	stencilFont.setPixelSize(9);
	stencilFont.setBold(true);
	stencilFont.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
	painter.setFont(stencilFont);
	QColor stencilInk;
	if (warm)
		stencilInk = withAlpha(QColor(tokens.accent), state.pressed ? 255 : 225);
	else
		stencilInk = dark ? QColor(0x8A, 0x84, 0x78, 170) : QColor(0xB8, 0xAF, 0x9E, 190);
	const QRectF stencilRect = r.adjusted(RackSkinDetail::EarWidth + 6, 0, -RackSkinDetail::EarWidth - 6, 0);
	RackSkinDetail::engraveText(painter, stencilRect, Qt::AlignCenter,
		warm ? QStringLiteral("INSTALL MODULE") : QStringLiteral("EMPTY BAY"), stencilInk, true);

	// Hover pre-heat: the bay's bezel warms amber, brightening under the
	// pressed finger - a lamp answer, not a button lift.
	if (warm)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.accent), state.pressed ? 190 : 120), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r, radius, radius);
	}

	// Keyboard focus: the thin service ring just inside the opening.
	if (state.focused)
	{
		painter.setPen(QPen(withAlpha(QColor(tokens.focusRing), 190), 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1, radius - 1);
	}
}

void RackSkin::paintInsertSeam(QPainter& painter, const QRect& rect, const ListChromeState& state, const SkinTokens& tokens) const
{
	// At rest the seam does not exist - the widget only calls this while
	// hovered, but keep the contract locally honest too.
	if (!state.hovered && !state.pressed)
		return;

	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);
	painter.setRenderHint(QPainter::Antialiasing);

	// A service slot heating up between the rail and the first unit:
	// strokes only. The machined groove first (a dark shadow line), then
	// the amber heat line over it, then the slot ticks marking where the
	// ears of the incoming unit will sit.
	const qreal y = rect.center().y();
	const qreal left = rect.left();
	const qreal right = rect.right();
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 90), 1));
	painter.drawLine(QPointF(left, y + 1.5), QPointF(right, y + 1.5));

	QColor amber(tokens.accent);
	// A wide faint pass under the line suggests the heat bleeding into the
	// metal (still a stroke - no fills, no discs).
	painter.setPen(QPen(withAlpha(amber, state.pressed ? 70 : 45), 4, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(left + RackSkinDetail::EarWidth, y), QPointF(right - RackSkinDetail::EarWidth, y));
	painter.setPen(QPen(withAlpha(amber, state.pressed ? 255 : 210), state.pressed ? 2.0 : 1.4, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(left, y), QPointF(right, y));

	// Ear ticks: short strokes at the ear grooves' positions.
	painter.setPen(QPen(withAlpha(amber, state.pressed ? 255 : 210), 1.4, Qt::SolidLine, Qt::FlatCap));
	painter.drawLine(QPointF(left + RackSkinDetail::EarWidth, y - 3), QPointF(left + RackSkinDetail::EarWidth, y + 3));
	painter.drawLine(QPointF(right - RackSkinDetail::EarWidth, y - 3), QPointF(right - RackSkinDetail::EarWidth, y + 3));
}
