/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "TintedIcon.h"

#include <QPainter>

QIcon TintedIcon::make(const QString& resource, const QColor& color, int size)
{
	QIcon base(resource);
	QPixmap pixmap = base.pixmap(QSize(size, size));
	if (pixmap.isNull())
		return base;

	// Keep the rendered glyph's alpha mask, replace its colour. CompositionMode_SourceIn
	// paints the fill only where the source already has coverage, so the result is the
	// same shape in the requested colour. The fill rect is in device-independent units;
	// any overshoot on a high-DPI pixmap is harmless because untouched pixels stay clear.
	QPainter painter(&pixmap);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(QRect(QPoint(0, 0), pixmap.deviceIndependentSize().toSize()), color);
	painter.end();

	QIcon icon(pixmap);
	// A disabled action must read as disabled from the glyph itself, not only
	// from the button chrome: bake a faded variant instead of relying on the
	// style's generated disabled look, which under the skins left the icon at
	// full strength (undo/redo looked clickable with an empty history).
	icon.addPixmap(faded(pixmap), QIcon::Disabled);
	return icon;
}

QPixmap TintedIcon::faded(const QPixmap& pixmap)
{
	// DestinationIn multiplies the existing alpha, keeping the glyph's shape
	// and colour while dropping its strength to roughly a third.
	QPixmap result = pixmap;
	QPainter painter(&result);
	painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	painter.fillRect(result.rect(), QColor(0, 0, 0, 90));
	painter.end();
	return result;
}
