/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FlatSkin.h"

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
// File-dialog pictograms: a round-capped stroke glyph in the body ink, on
// nothing. No tile and no colour per kind; what an entry is, its shape
// says (a folder, a sheet with two lines, decaying bars, a plug). The
// glyphs are drawn by hand because the SVG set's thin strokes smear at the
// dialog's 16px.
class FlatFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor ink(tokens.text);
		return paintedIcon([glyph, ink](QPainter& painter, const QRect&, int sizePx) {
			const qreal s = sizePx;
			painter.setPen(QPen(ink, qMax(1.2, s * 0.08), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter.setBrush(Qt::NoBrush);
			// Glyph coordinates are drawn on a 0..1 grid and spread 60% about
			// the centre: with no tile round it the glyph may use the whole
			// square, and a sheet at 16px keeps room for its two lines.
			const auto at = [s](qreal x, qreal y) {
				return QPointF(s * (0.5 + (x - 0.5) * 1.6), s * (0.5 + (y - 0.5) * 1.6));
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

QAbstractFileIconProvider* FlatSkin::fileIconProvider(const SkinTokens& tokens) const
{
	static FlatFileIconProvider iconProvider;
	iconProvider.updateTokens(tokens);
	return &iconProvider;
}
