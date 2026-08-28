/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/shared/SkinPaint.h"
#include "RackSkinDetail.h"

void RackSkin::paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const
{
	const bool dark = skinIsDark(tokens);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF r(rect);

	// Brushed-metal sheen: the rolled top edge falling into shadow, the same
	// finish as the card faceplates and the master rail - one machine.
	QLinearGradient sheen(r.topLeft(), r.bottomLeft());
	if (dark)
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 26));
		sheen.setColorAt(0.14, QColor(255, 255, 255, 10));
		sheen.setColorAt(0.55, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 52));
	}
	else
	{
		sheen.setColorAt(0.0, QColor(255, 255, 255, 120));
		sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
		sheen.setColorAt(1.0, QColor(0, 0, 0, 30));
	}
	painter.fillRect(r, sheen);

	// Horizontal brushing grain, same machine as the card faceplates.
	RackSkinDetail::paintBrushing(painter, r, dark, uint(qHash(QStringLiteral("top-panel-brush"))));

	// The caption-button block is the panel's right ear: a slightly recessed
	// zone behind the three machined caps, set off by a machined groove. The
	// caps are fixed-size (TitleBar::makeCaptionButton), so the groove sits at
	// the same scaled offset the layout gives them.
	const qreal capsWidth = qreal(GUIHelper::scale(40.0)) * 3.0;
	const qreal grooveX = r.right() - capsWidth - 6.0;
	const bool earFits = grooveX > r.left() + 120.0;
	if (earFits)
	{
		painter.fillRect(QRectF(grooveX, r.top(), r.right() - grooveX, r.height()),
			QColor(0, 0, 0, dark ? 52 : 20));
		painter.setPen(QPen(QColor(0, 0, 0, dark ? 120 : 60), 1));
		painter.drawLine(QPointF(grooveX, r.top()), QPointF(grooveX, r.bottom()));
		painter.setPen(QPen(QColor(255, 255, 255, dark ? 26 : 120), 1));
		painter.drawLine(QPointF(grooveX + 1, r.top()), QPointF(grooveX + 1, r.bottom()));
	}

	// Machined edges across the full rail (over the ear fill): lit top
	// chamfer, shadowed bottom groove against the menu bar below.
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 36 : 150), 1));
	painter.drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5));
	painter.setPen(QPen(QColor(0, 0, 0, dark ? 150 : 70), 1));
	painter.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5));

	// Two rail screws bolting the top panel down: one at the left end before
	// the engraved designation, one on the blank panel before the caption
	// ear. Slot angles differ - hand-tightened, like everywhere else.
	const uint seed = uint(qHash(QStringLiteral("top-panel")));
	RackSkinDetail::paintScrew(painter, QPointF(r.left() + 10.0, r.center().y()), 4.0, qreal(seed % 180u), dark);
	if (earFits)
		RackSkinDetail::paintScrew(painter, QPointF(grooveX - 12.0, r.center().y()), 4.0, qreal((seed + 73u) % 180u), dark);
}
