/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackFileIcons.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include "Editor/skins/shared/SkinFileIcons.h"

namespace
{
class RackFileIconProvider : public SkinFileIconProvider
{
protected:
	QIcon makeIcon(Glyph glyph, const SkinTokens& tokens) const override
	{
		const QColor amber(tokens.accent);
		return paintedIcon([glyph, amber](QPainter& painter, const QRect&, int sizePx) {
			const qreal s = sizePx;
			const qreal outlineWidth = qMax(1.0, s * 0.055);

			// Paper sheet with a turned corner; detail() adds the per-type
			// content on the page.
			const auto paperSheet = [&](const std::function<void()>& detail) {
				QPainterPath sheet;
				sheet.moveTo(s * 0.24, s * 0.12);
				sheet.lineTo(s * 0.62, s * 0.12);
				sheet.lineTo(s * 0.76, s * 0.26);
				sheet.lineTo(s * 0.76, s * 0.88);
				sheet.lineTo(s * 0.24, s * 0.88);
				sheet.closeSubpath();
				QLinearGradient paper(0, s * 0.12, 0, s * 0.88);
				paper.setColorAt(0.0, QColor(0xF7, 0xF4, 0xEA));
				paper.setColorAt(1.0, QColor(0xE3, 0xDF, 0xD1));
				painter.setPen(QPen(QColor(0x8A, 0x86, 0x78), outlineWidth));
				painter.setBrush(paper);
				painter.drawPath(sheet);
				// The turned corner: a shaded triangle with its own crease.
				QPainterPath fold;
				fold.moveTo(s * 0.62, s * 0.12);
				fold.lineTo(s * 0.62, s * 0.26);
				fold.lineTo(s * 0.76, s * 0.26);
				fold.closeSubpath();
				painter.setBrush(QColor(0xCD, 0xC8, 0xB6));
				painter.drawPath(fold);
				detail();
			};

			switch (glyph)
			{
			case Glyph::Folder:
			{
				// Manila folder: tab behind, warm body, worklight on the top
				// edge. Same manila in dark and light - cardboard is cardboard.
				QPainterPath body;
				body.moveTo(s * 0.10, s * 0.80);
				body.lineTo(s * 0.10, s * 0.24);
				body.lineTo(s * 0.40, s * 0.24);
				body.lineTo(s * 0.48, s * 0.34);
				body.lineTo(s * 0.90, s * 0.34);
				body.lineTo(s * 0.90, s * 0.80);
				body.closeSubpath();
				QLinearGradient manila(0, s * 0.24, 0, s * 0.80);
				manila.setColorAt(0.0, QColor(0xE8, 0xC8, 0x7E));
				manila.setColorAt(1.0, QColor(0xC7, 0xA1, 0x52));
				painter.setPen(QPen(QColor(0x8F, 0x6F, 0x2E), outlineWidth));
				painter.setBrush(manila);
				painter.drawPath(body);
				// Catch-light along the tab edge.
				painter.setPen(QPen(QColor(0xFF, 0xEC, 0xBC), qMax(1.0, s * 0.04)));
				painter.drawLine(QPointF(s * 0.13, s * 0.27), QPointF(s * 0.38, s * 0.27));
				break;
			}
			case Glyph::ConfigFile:
				paperSheet([&]() {
					painter.setPen(QPen(QColor(0x9A, 0x94, 0x82), qMax(1.0, s * 0.05)));
					painter.drawLine(QPointF(s * 0.32, s * 0.46), QPointF(s * 0.68, s * 0.46));
					painter.drawLine(QPointF(s * 0.32, s * 0.58), QPointF(s * 0.68, s * 0.58));
					painter.drawLine(QPointF(s * 0.32, s * 0.70), QPointF(s * 0.56, s * 0.70));
				});
				break;
			case Glyph::AudioFile:
				paperSheet([&]() {
					// The label strip a tape reel would carry: an amber trace.
					painter.setPen(QPen(amber, qMax(1.0, s * 0.06), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
					QPainterPath wave;
					wave.moveTo(s * 0.31, s * 0.60);
					wave.lineTo(s * 0.41, s * 0.46);
					wave.lineTo(s * 0.53, s * 0.70);
					wave.lineTo(s * 0.66, s * 0.52);
					painter.drawPath(wave);
				});
				break;
			case Glyph::PluginFile:
				paperSheet([&]() {
					// A DIP chip on the sheet: the outboard unit's brain.
					painter.setPen(QPen(QColor(0x2A, 0x2E, 0x33), qMax(1.0, s * 0.04)));
					painter.setBrush(QColor(0x3A, 0x40, 0x47));
					painter.drawRect(QRectF(s * 0.38, s * 0.48, s * 0.24, s * 0.20));
					painter.drawLine(QPointF(s * 0.43, s * 0.48), QPointF(s * 0.43, s * 0.41));
					painter.drawLine(QPointF(s * 0.50, s * 0.48), QPointF(s * 0.50, s * 0.41));
					painter.drawLine(QPointF(s * 0.57, s * 0.48), QPointF(s * 0.57, s * 0.41));
				});
				break;
			case Glyph::GenericFile:
				paperSheet([]() {});
				break;
			case Glyph::Drive:
			{
				// A rack drive slab: brushed dark metal, milled slot, power LED.
				QLinearGradient metal(0, s * 0.30, 0, s * 0.72);
				metal.setColorAt(0.0, QColor(0x3C, 0x44, 0x4C));
				metal.setColorAt(1.0, QColor(0x20, 0x26, 0x2B));
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), outlineWidth));
				painter.setBrush(metal);
				painter.drawRoundedRect(QRectF(s * 0.10, s * 0.30, s * 0.80, s * 0.42), s * 0.04, s * 0.04);
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), qMax(1.0, s * 0.04)));
				painter.drawLine(QPointF(s * 0.18, s * 0.60), QPointF(s * 0.56, s * 0.60));
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor(0x7C, 0xE8, 0xA8));
				painter.drawEllipse(QPointF(s * 0.78, s * 0.58), s * 0.045, s * 0.045);
				break;
			}
			case Glyph::Computer:
			{
				// The studio's CRT monitor: dark bezel, powered screen.
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), outlineWidth));
				painter.setBrush(QColor(0x2C, 0x32, 0x38));
				painter.drawRoundedRect(QRectF(s * 0.12, s * 0.16, s * 0.76, s * 0.50), s * 0.05, s * 0.05);
				painter.setPen(Qt::NoPen);
				painter.setBrush(QColor(0x14, 0x1A, 0x14));
				painter.drawRect(QRectF(s * 0.20, s * 0.24, s * 0.60, s * 0.34));
				painter.setBrush(QColor(0x7C, 0xE8, 0xA8));
				painter.drawRect(QRectF(s * 0.24, s * 0.30, s * 0.22, s * 0.045));
				painter.setPen(QPen(QColor(0x0E, 0x12, 0x15), outlineWidth));
				painter.setBrush(QColor(0x2C, 0x32, 0x38));
				painter.drawRect(QRectF(s * 0.44, s * 0.66, s * 0.12, s * 0.10));
				painter.drawRect(QRectF(s * 0.30, s * 0.76, s * 0.40, s * 0.06));
				break;
			}
			}
		});
	}
};
}

QFileIconProvider* rackFileIconProvider(const SkinTokens& tokens)
{
	static RackFileIconProvider iconProvider;
	iconProvider.updateTokens(tokens);
	return &iconProvider;
}
