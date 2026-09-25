/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QFileDialog>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/shared/SkinFileIcons.h"
#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// File-dialog pictograms in Soft's tile language: every entry is a small
// rounded pastel tile (the picker and reference-card tiles' 32% corner)
// with a round-capped stroke glyph in the tiles' near-white ink. The
// glyphs are the reference card's own pictograms (the Include sheet, the
// Convolution bars, the VST plug) redrawn by hand, because the SVG set's
// thin strokes smear at the dialog's 16px. Two pastels only, both already
// in the dialog's navigation row: places you open (folders, drives, the
// computer) keep the folder buttons' warm tint, files keep the accent
// the Include tile wears.
class SoftFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const bool dark = skinIsDark(tokens);
		const bool place = glyph == Glyph::Folder || glyph == Glyph::Drive || glyph == Glyph::Computer;
		const QColor tile = softPastelize(QColor(place ? tokens.warning : tokens.accent), dark);
		const QColor ink(QStringLiteral("#FAFAFC"));
		return paintedIcon([glyph, tile, ink](QPainter& painter, const QRect&, int sizePx) {
			const qreal s = sizePx;
			const QRectF tileRect(s * 0.05, s * 0.05, s * 0.90, s * 0.90);
			painter.setPen(Qt::NoPen);
			painter.setBrush(tile);
			painter.drawRoundedRect(tileRect, tileRect.width() * 0.32, tileRect.height() * 0.32);

			painter.setPen(QPen(ink, qMax(1.2, s * 0.085), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			// Glyph coordinates are drawn on a 0..1 grid and spread 15% about the
			// tile centre, so the stroke glyph fills the tile the way the picker's
			// glyphs do and a sheet at 16px still has room for its two lines.
			const auto at = [s](qreal x, qreal y) {
				return QPointF(s * (0.5 + (x - 0.5) * 1.15), s * (0.5 + (y - 0.5) * 1.15));
			};

			const auto sheet = [&]() {
				QPainterPath path;
				path.moveTo(at(0.34, 0.24));
				path.lineTo(at(0.55, 0.24));
				path.lineTo(at(0.66, 0.35));
				path.lineTo(at(0.66, 0.76));
				path.lineTo(at(0.34, 0.76));
				path.closeSubpath();
				painter.drawPath(path);
			};

			switch (glyph)
			{
			case Glyph::Folder:
			{
				QPainterPath path;
				path.moveTo(at(0.27, 0.70));
				path.lineTo(at(0.27, 0.31));
				path.lineTo(at(0.44, 0.31));
				path.lineTo(at(0.50, 0.38));
				path.lineTo(at(0.73, 0.38));
				path.lineTo(at(0.73, 0.70));
				path.closeSubpath();
				painter.drawPath(path);
				break;
			}
			case Glyph::ConfigFile:
				sheet();
				painter.drawLine(at(0.43, 0.51), at(0.57, 0.51));
				painter.drawLine(at(0.43, 0.63), at(0.57, 0.63));
				break;
			case Glyph::AudioFile:
				// The Convolution tile's decaying bars: an impulse fading out.
				painter.drawLine(at(0.34, 0.30), at(0.34, 0.70));
				painter.drawLine(at(0.50, 0.37), at(0.50, 0.63));
				painter.drawLine(at(0.66, 0.44), at(0.66, 0.56));
				break;
			case Glyph::PluginFile:
				// The VST tile's plug: two prongs, a rounded body, a cord.
				painter.drawLine(at(0.43, 0.25), at(0.43, 0.38));
				painter.drawLine(at(0.57, 0.25), at(0.57, 0.38));
				painter.drawRoundedRect(QRectF(at(0.33, 0.38), at(0.67, 0.60)), s * 0.06, s * 0.06);
				painter.drawLine(at(0.50, 0.60), at(0.50, 0.75));
				break;
			case Glyph::GenericFile:
				sheet();
				break;
			case Glyph::Drive:
				painter.drawRoundedRect(QRectF(at(0.25, 0.36), at(0.75, 0.64)), s * 0.07, s * 0.07);
				painter.setPen(Qt::NoPen);
				painter.setBrush(ink);
				painter.drawEllipse(at(0.62, 0.50), s * 0.05, s * 0.05);
				break;
			case Glyph::Computer:
				painter.drawRoundedRect(QRectF(at(0.26, 0.27), at(0.74, 0.58)), s * 0.06, s * 0.06);
				painter.drawLine(at(0.50, 0.58), at(0.50, 0.70));
				painter.drawLine(at(0.38, 0.72), at(0.62, 0.72));
				break;
			}
		});
	}
};
}

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
		const int side = logical;
		QPixmap pixmap(side, side);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(tile);
		painter.drawRoundedRect(QRectF(0, 0, side, side), side * 0.32, side * 0.32);
		const int glyphSide = qMax(10, qRound(logical * 0.66));
		const QPixmap glyph = GUIHelper::tintedIcon(resource, QColor(QStringLiteral("#FAFAFC")), glyphSide)
			.pixmap(QSize(glyphSide, glyphSide));
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

void SoftSkin::installFileIconProvider(QFileDialog* dialog, const SkinTokens& tokens)
{
	static SoftFileIconProvider iconProvider;
	iconProvider.updateTokens(tokens);
	dialog->setIconProvider(&iconProvider);
}
