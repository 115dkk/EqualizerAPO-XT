/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QPainter>

#include "MatrixSkinDetail.h"

void MatrixSkin::paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const
{
		painter.setRenderHint(QPainter::Antialiasing, false);

		// The hook carries no mode flag; infer it from the surface lightness
		// (the studioIsDark pattern). The light border ink needs more alpha
		// than the dark one to stay visible as graph paper on white.
		QColor grid(tokens.border);
		grid.setAlpha(tokens.dark ? 55 : 90);
		painter.setPen(QPen(grid, 1));
		for (int x = rect.left() + MatrixMetrics::gridPitch; x < rect.right(); x += MatrixMetrics::gridPitch)
			painter.drawLine(x, rect.top(), x, rect.bottom());

		painter.setPen(QPen(QColor(tokens.border), 1));
		painter.drawLine(rect.left(), rect.bottom() - 3, rect.right(), rect.bottom() - 3);
	}
