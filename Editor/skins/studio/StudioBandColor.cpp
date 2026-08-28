/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioBandColor.h"

#include <QPainter>
#include <QWidget>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"
#include "filters/BiQuad.h"

namespace StudioBandColors
{
QString studioBandHex(const QString& family, bool dark)
{
	if (family == QLatin1String("shelf"))
		return dark ? QStringLiteral("#44D7A4") : QStringLiteral("#0C9E72");
	if (family == QLatin1String("pass"))
		return dark ? QStringLiteral("#A66CFF") : QStringLiteral("#8A4DFF");
	if (family == QLatin1String("notch"))
		return dark ? QStringLiteral("#FF7FA8") : QStringLiteral("#DB4D7E");
	return dark ? QStringLiteral("#5B8CFF") : QStringLiteral("#2F6BFF");
}

QString studioBandFamilyForBiQuadType(int type)
{
	switch (type)
	{
	case BiQuad::LOW_SHELF:
	case BiQuad::HIGH_SHELF:
		return QStringLiteral("shelf");
	case BiQuad::LOW_PASS:
	case BiQuad::HIGH_PASS:
	case BiQuad::BAND_PASS:
		return QStringLiteral("pass");
	case BiQuad::NOTCH:
	case BiQuad::ALL_PASS:
		return QStringLiteral("notch");
	default:
		return QStringLiteral("peak");
	}
}

QString studioBandFamilyForBadgeToken(const QString& token)
{
	if (token.startsWith(QLatin1String("LS")) || token.startsWith(QLatin1String("HS")))
		return QStringLiteral("shelf");
	if (token.startsWith(QLatin1String("LP")) || token.startsWith(QLatin1String("HP")) || token.startsWith(QLatin1String("BP")))
		return QStringLiteral("pass");
	if (token.startsWith(QLatin1String("NO")) || token.startsWith(QLatin1String("AP")))
		return QStringLiteral("notch");
	return QStringLiteral("peak");
}

QColor studioBandPaintColor(const QPainter& painter, const SkinTokens& tokens)
{
	QString hex = tokens.accent;
	if (painter.device() != nullptr && painter.device()->devType() == QInternal::Widget)
	{
		const QVariant family = static_cast<const QWidget*>(painter.device())->property("studioBand");
		if (family.isValid())
			hex = studioBandHex(family.toString(), skinIsDark(tokens));
	}
	return QColor(hex);
}
}
