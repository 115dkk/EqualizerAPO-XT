/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QtMath>

#include "Editor/helpers/GUIHelper.h"

// One icon, several pre-rendered sizes (16px for the File menu rows up to
// the 22px toolbar size and beyond), so Qt never stretches a tile. The
// tile matches SoftFilterPicker's category tiles: a rounded square at 32%
// corner radius with the glyph inked in the picker's near-white literal.
QIcon SoftSkin::softTileIcon(const QString& resource, const QColor& tile)
{
	QIcon icon;
	// 44/64 keep the tile crisp on 2x displays (22/32 logical at DPR 2).
	for (const int logical : { 16, 18, 20, 22, 24, 32, 44, 64 })
	{
		const int side = GUIHelper::scale(double(logical));
		QPixmap pixmap(side, side);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(tile);
		painter.drawRoundedRect(QRectF(0, 0, side, side), side * 0.32, side * 0.32);
		const int glyphSide = qMax(10, qRound(logical * 0.66));
		const QPixmap glyph = GUIHelper::tintedIcon(resource, QColor(QStringLiteral("#FAFAFC")), glyphSide)
			.pixmap(GUIHelper::scale(QSize(glyphSide, glyphSide)));
		// Centre by the glyph's LOGICAL size: on high-DPR displays
		// QIcon::pixmap returns a pixmap whose width() is physical pixels
		// (dpr baked in), and drawPixmap honors the dpr - centring by
		// width() shoved the glyph toward the top-left at 200% scale.
		const QSizeF glyphLogical = glyph.deviceIndependentSize();
		painter.drawPixmap(QPointF((side - glyphLogical.width()) / 2.0,
			(side - glyphLogical.height()) / 2.0), glyph);
		painter.end();
		icon.addPixmap(pixmap);
		// The whole tile fades when the action is disabled, the shared
		// disabled-glyph recipe (undo/redo on an empty history).
		icon.addPixmap(GUIHelper::fadedPixmap(pixmap), QIcon::Disabled);
	}
	return icon;
}
