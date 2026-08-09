/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#pragma once

#include <QtGlobal>

class QColor;
class QPainter;
class QPointF;
class QRectF;
class QString;

namespace RackSkinDetail
{
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
