/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TitleBarChrome.h"

#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterStateGuard>
#include <QtMath>

#include "Editor/skins/matrix/MatrixSkinDetail.h"
#include "Editor/skins/rack/RackPalette.h"
#include "Editor/skins/rack/RackSkinDetail.h"
#include "Editor/skins/shared/SkinPaint.h"

void TitleBarChrome::paintStudio(QPainter& painter, const QRect& rect, const SkinTokens& tokens)
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

void TitleBarChrome::paintRack(QPainter& painter, const QRect& rect, const SkinTokens& tokens)
{
	const bool dark = skinIsDark(tokens);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF r(rect);

	// Brushed-metal sheen: the rolled top edge falling into shadow, the same
	// finish as the card faceplates and the master rail - one machine.
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, RackPalette::light(26));
		sheen.setColorAt(0.14, RackPalette::light(10));
		sheen.setColorAt(0.55, RackPalette::light(0));
		sheen.setColorAt(1.0, RackPalette::shadow(52));
	}
	else
	{
		sheen.setColorAt(0.0, RackPalette::light(120));
		sheen.setColorAt(0.5, RackPalette::light(0));
		sheen.setColorAt(1.0, RackPalette::shadow(30));
	}
	painter.fillRect(r, sheen);

	// Horizontal brushing grain, same machine as the card faceplates.
	RackSkinDetail::paintBrushing(painter, r, dark, uint(qHash(QStringLiteral("top-panel-brush"))));

	// The caption-button block is the panel's right ear: a slightly recessed
	// zone behind the three machined caps, set off by a machined groove. The
	// caps are fixed-size (TitleBar::makeCaptionButton), so the groove sits at
	// the same scaled offset the layout gives them.
	const qreal capsWidth = qreal(40) * 3.0;
	const qreal grooveX = r.right() - capsWidth - 6.0;
	const bool earFits = grooveX > r.left() + 120.0;
	if (earFits)
	{
		painter.fillRect(QRectF(grooveX, r.top(), r.right() - grooveX, r.height()),
			RackPalette::shadow(dark ? 52 : 20));
		painter.setPen(QPen(RackPalette::shadow(dark ? 120 : 60), 1));
		painter.drawLine(QPointF(grooveX, r.top()), QPointF(grooveX, r.bottom()));
		painter.setPen(QPen(RackPalette::light(dark ? 26 : 120), 1));
		painter.drawLine(QPointF(grooveX + 1, r.top()), QPointF(grooveX + 1, r.bottom()));
	}

	// Machined edges across the full rail (over the ear fill): lit top
	// chamfer, shadowed bottom groove against the menu bar below.
	painter.setPen(QPen(RackPalette::light(dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5));
	painter.setPen(QPen(RackPalette::shadow(dark ? 150 : 70), 1));
	painter.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

	// Two rail screws bolting the top panel down: one at the left end before
	// the engraved designation, one on the blank panel before the caption
	// ear. Slot angles differ - hand-tightened, like everywhere else.
	const uint seed = uint(qHash(QStringLiteral("top-panel")));
	RackSkinDetail::paintScrew(painter, QPointF(r.left() + 10.0, r.center().y()), 4.0, qreal(seed % 180u), dark);
	if (earFits)
		RackSkinDetail::paintScrew(painter, QPointF(grooveX - 12.0, r.center().y()), 4.0, qreal((seed + 73u) % 180u), dark);
}

void TitleBarChrome::paintMatrix(QPainter& painter, const QRect& rect, const SkinTokens& tokens)
{
	painter.setRenderHint(QPainter::Antialiasing, false);

	// The light border ink needs more alpha than the dark one to stay
	// visible as graph paper on white.
	QColor grid(tokens.border);
	grid.setAlpha(tokens.dark ? 55 : 90);
	painter.setPen(QPen(grid, 1));
	for (int x = rect.left() + MatrixMetrics::gridPitch; x < rect.right(); x += MatrixMetrics::gridPitch)
		painter.drawLine(x, rect.top(), x, rect.bottom());

	painter.setPen(QPen(QColor(tokens.border), 1));
	painter.drawLine(rect.left(), rect.bottom() - 3, rect.right(), rect.bottom() - 3);
}

void TitleBarChrome::paint(const QString& skinId, QPainter& painter, const QRect& rect, const SkinTokens& tokens)
{
	if (skinId == QLatin1String("studio"))
		paintStudio(painter, rect, tokens);
	else if (skinId == QLatin1String("rack"))
		paintRack(painter, rect, tokens);
	else if (skinId == QLatin1String("matrix"))
		paintMatrix(painter, rect, tokens);
}
