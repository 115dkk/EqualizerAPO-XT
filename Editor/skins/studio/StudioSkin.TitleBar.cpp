/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioSkin.h"

#include <QPainter>
#include <QPainterStateGuard>

#include "Editor/skins/shared/SkinPaint.h"
#include "Editor/skins/shared/SkinSupport.h"

void StudioSkin::paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	QPainterStateGuard painterState(&painter);

	// The 1px lighter top edge, quieter than a panel's reflection.
	painter.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 1.0),
		QColor(255, 255, 255, dark ? 30 : 235));

	painter.setRenderHint(QPainter::Antialiasing);
	const double span = rect.width() * 0.42;
	const double x0 = rect.left() + (rect.width() - span) / 2.0;
	const double y = rect.top() + 0.5;
	QLinearGradient bloom(x0, y, x0 + span, y);
	bloom.setColorAt(0.0, withAlpha(tokens.accent, 0));
	bloom.setColorAt(0.35, withAlpha(tokens.accent, dark ? 70 : 55));
	bloom.setColorAt(0.7, withAlpha(tokens.accent2, dark ? 52 : 42));
	bloom.setColorAt(1.0, withAlpha(tokens.accent2, 0));
	QPen bloomPen(QBrush(bloom), 4.0);
	bloomPen.setCapStyle(Qt::RoundCap);
	painter.setPen(bloomPen);
	painter.drawLine(QPointF(x0, y), QPointF(x0 + span, y));
	QLinearGradient core(x0, y, x0 + span, y);
	core.setColorAt(0.0, withAlpha(tokens.accent, 0));
	core.setColorAt(0.35, withAlpha(tokens.accent, dark ? 215 : 195));
	core.setColorAt(0.7, withAlpha(tokens.accent2, dark ? 175 : 155));
	core.setColorAt(1.0, withAlpha(tokens.accent2, 0));
	QPen corePen(QBrush(core), 1.5);
	corePen.setCapStyle(Qt::RoundCap);
	painter.setPen(corePen);
	painter.drawLine(QPointF(x0, y), QPointF(x0 + span, y));
}
