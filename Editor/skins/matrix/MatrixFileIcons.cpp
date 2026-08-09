/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixFileIcons.h"

#include <QPainter>
#include <QPainterPath>

#include "Editor/skins/shared/SkinFileIcons.h"

namespace
{

class MatrixFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor ink(tokens.text);
		return paintedIcon([glyph, ink](QPainter& painter, const QRect&, int sizePx) {
			const qreal s = sizePx;
			QColor faint(ink);
			faint.setAlpha(26);
			painter.setPen(QPen(ink, qMax(1.0, s * 0.065), Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
			painter.setBrush(faint);

			// Chamfer: one cut corner, the HUD's way of saying "panel".
			const auto chamferedRect = [&](qreal x, qreal y, qreal w, qreal h) {
				const qreal cut = qMin(w, h) * 0.28;
				QPainterPath path;
				path.moveTo(x, y);
				path.lineTo(x + w - cut, y);
				path.lineTo(x + w, y + cut);
				path.lineTo(x + w, y + h);
				path.lineTo(x, y + h);
				path.closeSubpath();
				painter.drawPath(path);
			};

			switch (glyph)
			{
			case Glyph::Folder:
			{
				QPainterPath path;
				path.moveTo(s * 0.12, s * 0.78);
				path.lineTo(s * 0.12, s * 0.26);
				path.lineTo(s * 0.42, s * 0.26);
				path.lineTo(s * 0.48, s * 0.36);
				path.lineTo(s * 0.88, s * 0.36);
				path.lineTo(s * 0.88, s * 0.66);
				path.lineTo(s * 0.80, s * 0.78);
				path.closeSubpath();
				painter.drawPath(path);
				painter.fillRect(QRectF(s * 0.18, s * 0.44, s * 0.10, s * 0.07), ink);
				break;
			}
			case Glyph::ConfigFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				painter.drawLine(QPointF(s * 0.33, s * 0.46), QPointF(s * 0.67, s * 0.46));
				painter.drawLine(QPointF(s * 0.33, s * 0.58), QPointF(s * 0.67, s * 0.58));
				painter.drawLine(QPointF(s * 0.33, s * 0.70), QPointF(s * 0.55, s * 0.70));
				break;
			case Glyph::AudioFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				painter.fillRect(QRectF(s * 0.33, s * 0.58, s * 0.08, s * 0.16), ink);
				painter.fillRect(QRectF(s * 0.45, s * 0.44, s * 0.08, s * 0.30), ink);
				painter.fillRect(QRectF(s * 0.57, s * 0.64, s * 0.08, s * 0.10), ink);
				break;
			case Glyph::PluginFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				painter.drawRect(QRectF(s * 0.38, s * 0.48, s * 0.24, s * 0.20));
				painter.drawLine(QPointF(s * 0.44, s * 0.48), QPointF(s * 0.44, s * 0.40));
				painter.drawLine(QPointF(s * 0.56, s * 0.48), QPointF(s * 0.56, s * 0.40));
				break;
			case Glyph::GenericFile:
				chamferedRect(s * 0.24, s * 0.12, s * 0.52, s * 0.76);
				break;
			case Glyph::Drive:
				chamferedRect(s * 0.12, s * 0.30, s * 0.76, s * 0.40);
				painter.drawLine(QPointF(s * 0.20, s * 0.58), QPointF(s * 0.50, s * 0.58));
				painter.fillRect(QRectF(s * 0.70, s * 0.52, s * 0.10, s * 0.10), ink);
				break;
			case Glyph::Computer:
				chamferedRect(s * 0.14, s * 0.18, s * 0.72, s * 0.46);
				painter.fillRect(QRectF(s * 0.22, s * 0.28, s * 0.24, s * 0.06), ink);
				painter.drawLine(QPointF(s * 0.50, s * 0.64), QPointF(s * 0.50, s * 0.76));
				painter.drawLine(QPointF(s * 0.34, s * 0.80), QPointF(s * 0.66, s * 0.80));
				break;
			}
		});
	}
};

}

QFileIconProvider* matrixFileIconProvider(const SkinTokens& tokens)
{
	static MatrixFileIconProvider iconProvider;
	iconProvider.updateTokens(tokens);
	return &iconProvider;
}
