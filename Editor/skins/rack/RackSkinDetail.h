/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
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
// frame both popped and glared). Used by the QSS border and the bezel.
inline QString darkEngagedFrame()
{
	return QStringLiteral("#8C8578");
}

inline constexpr int EarWidth = 20;
inline constexpr qreal NameplateWidth = 78.0;
inline constexpr qreal NameplateHeight = 22.0;

void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text,
	const QColor& body, bool dark);
void paintScrew(QPainter& painter, const QPointF& center, qreal radius,
	qreal slotDegrees, bool dark);
void paintLed(QPainter& painter, const QPointF& center, qreal radius,
	const QColor& litColor, bool lit, bool dark);
void paintBrushing(QPainter& painter, const QRectF& rect, bool dark, uint seed);
void paintJack(QPainter& painter, const QPointF& center, bool dark);
void paintGrain(QPainter& painter, const QRectF& rect, const QColor& ink, uint seed);
}
