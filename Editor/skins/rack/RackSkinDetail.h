/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QtGlobal>
#include <QString>

class QColor;
class QPainter;
class QPointF;
class QRectF;
class QString;

namespace RackSkinDetail
{
// The dark-mode engaged (selected / focused) unit frame: a warm neutral
// instead of the amber accent (brightness round 2, issue #301: the amber
// frame both popped and glared). Used by the QSS border and the bezel, and
// by every cap that answers the pointer, latches or holds focus, so a unit
// carries one glow colour; rack_dark.qss spells it for the stylesheet caps.
inline QString darkEngagedFrame()
{
	return QStringLiteral("#8C8578");
}

inline constexpr int EarWidth = 20;
inline constexpr qreal NameplateWidth = 78.0;
inline constexpr qreal NameplateHeight = 22.0;

void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text,
	const QColor& body, bool dark);
// The same double pass with the recess colour given: a powered-down label
// passes a fainter one.
void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text,
	const QColor& body, const QColor& relief);
void paintScrew(QPainter& painter, const QPointF& center, qreal radius,
	qreal slotDegrees, bool dark);
void paintLed(QPainter& painter, const QPointF& center, qreal radius,
	const QColor& litColor, bool lit, bool dark);
// The same lamp with the reach of its lit halo given; the overload above
// lets it reach 3.2 radii.
void paintLed(QPainter& painter, const QPointF& center, qreal radius, qreal haloRadius,
	const QColor& litColor, bool lit, bool dark);
void paintBrushing(QPainter& painter, const QRectF& rect, bool dark, uint seed);
// The same grain in another ink: each line replaces ink's alpha with
// baseAlpha plus the line's hash share. The overload above passes the
// palette's BrushingGrain and a base alpha of 4 (dark) or 5 (light).
void paintBrushing(QPainter& painter, const QRectF& rect, const QColor& ink, int baseAlpha, uint seed);
void paintJack(QPainter& painter, const QPointF& center, bool dark);
void paintGrain(QPainter& painter, const QRectF& rect, const QColor& ink, uint seed);
}
