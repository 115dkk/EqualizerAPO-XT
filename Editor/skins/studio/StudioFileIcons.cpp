/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioFileIcons.h"

#include <QPainter>
#include <QPainterPath>

QIcon StudioFileIconProvider::makeIcon(Glyph glyph, const SkinTokens& tokens) const
{
	const QColor ink(tokens.text);
	return paintedIcon([glyph, ink](QPainter& painter, const QRect&, int sizePx) {
		const qreal s = sizePx;
		painter.setPen(QPen(ink, qMax(1.1, s * 0.08), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);

		const auto docOutline = [&]() {
			QPainterPath path;
			path.moveTo(s * 0.24, s * 0.14);
			path.lineTo(s * 0.62, s * 0.14);
			path.lineTo(s * 0.76, s * 0.28);
			path.lineTo(s * 0.76, s * 0.86);
			path.lineTo(s * 0.24, s * 0.86);
			path.closeSubpath();
			painter.drawPath(path);
			painter.drawLine(QPointF(s * 0.62, s * 0.14), QPointF(s * 0.62, s * 0.28));
			painter.drawLine(QPointF(s * 0.62, s * 0.28), QPointF(s * 0.76, s * 0.28));
		};

		switch (glyph)
		{
		case Glyph::Folder:
		{
			QPainterPath path;
			path.moveTo(s * 0.12, s * 0.78);
			path.lineTo(s * 0.12, s * 0.26);
			path.lineTo(s * 0.40, s * 0.26);
			path.lineTo(s * 0.48, s * 0.36);
			path.lineTo(s * 0.88, s * 0.36);
			path.lineTo(s * 0.88, s * 0.78);
			path.closeSubpath();
			painter.drawPath(path);
			break;
		}
		case Glyph::ConfigFile:
			docOutline();
			painter.drawLine(QPointF(s * 0.34, s * 0.50), QPointF(s * 0.66, s * 0.50));
			painter.drawLine(QPointF(s * 0.34, s * 0.66), QPointF(s * 0.58, s * 0.66));
			break;
		case Glyph::AudioFile:
		{
			docOutline();
			QPainterPath wave;
			wave.moveTo(s * 0.32, s * 0.60);
			wave.lineTo(s * 0.42, s * 0.46);
			wave.lineTo(s * 0.54, s * 0.70);
			wave.lineTo(s * 0.66, s * 0.54);
			painter.drawPath(wave);
			break;
		}
		case Glyph::PluginFile:
			docOutline();
			painter.drawRect(QRectF(s * 0.38, s * 0.48, s * 0.24, s * 0.22));
			painter.drawLine(QPointF(s * 0.44, s * 0.48), QPointF(s * 0.44, s * 0.40));
			painter.drawLine(QPointF(s * 0.56, s * 0.48), QPointF(s * 0.56, s * 0.40));
			break;
		case Glyph::GenericFile:
			docOutline();
			break;
		case Glyph::Drive:
			painter.drawRoundedRect(QRectF(s * 0.12, s * 0.32, s * 0.76, s * 0.38), s * 0.06, s * 0.06);
			painter.drawLine(QPointF(s * 0.20, s * 0.58), QPointF(s * 0.52, s * 0.58));
			painter.setBrush(ink);
			painter.drawEllipse(QPointF(s * 0.76, s * 0.58), s * 0.035, s * 0.035);
			break;
		case Glyph::Computer:
			painter.drawRoundedRect(QRectF(s * 0.14, s * 0.18, s * 0.72, s * 0.46), s * 0.05, s * 0.05);
			painter.drawLine(QPointF(s * 0.50, s * 0.64), QPointF(s * 0.50, s * 0.76));
			painter.drawLine(QPointF(s * 0.34, s * 0.80), QPointF(s * 0.66, s * 0.80));
			break;
		}
	});
}
