/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#pragma once

#include <QColor>
#include <QString>

class QPainter;
struct SkinTokens;

namespace StudioBandColors
{
QString studioBandHex(const QString& family, bool dark);
QString studioBandFamilyForBiQuadType(int type);
QString studioBandFamilyForBadgeToken(const QString& token);
QColor studioBandPaintColor(const QPainter& painter, const SkinTokens& tokens);
}
