/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkinDetail.h"

#include <QPainter>
#include <QPixmapCache>
#include <QRadialGradient>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

namespace RackSkinDetail
{
void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, bool dark)
{
	painter.setPen(dark ? QColor(0, 0, 0, 170) : QColor(255, 255, 255, 200));
	painter.drawText(rect.translated(0, 1), flags, text);
	painter.setPen(body);
	painter.drawText(rect, flags, text);
}

void paintScrew(QPainter& painter, const QPointF& center, qreal radius, qreal slotDegrees, bool dark)
{
	QRadialGradient body(center - QPointF(radius * 0.35, radius * 0.35), radius * 2.1);
	if (dark)
	{
		body.setColorAt(0.0, QColor(0x9A, 0xA4, 0xAC));
		body.setColorAt(0.55, QColor(0x4E, 0x57, 0x5E));
		body.setColorAt(1.0, QColor(0x23, 0x28, 0x2C));
	}
	else
	{
		body.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFC));
		body.setColorAt(0.55, QColor(0xC4, 0xBD, 0xAE));
		body.setColorAt(1.0, QColor(0x8E, 0x86, 0x76));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 200) : QColor(0x6B, 0x62, 0x52), 1));
	painter.setBrush(body);
	painter.drawEllipse(center, radius, radius);

	const qreal rad = qDegreesToRadians(slotDegrees);
	const QPointF dir(qCos(rad), qSin(rad));
	const QPointF a = center - dir * (radius - 1.2);
	const QPointF b = center + dir * (radius - 1.2);
	painter.setPen(QPen(dark ? QColor(10, 12, 14, 230) : QColor(60, 54, 44, 220), 1.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a, b);
	painter.setPen(QPen(QColor(255, 255, 255, dark ? 60 : 170), 0.8, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a + QPointF(0, 1), b + QPointF(0, 1));
}

void paintLed(QPainter& painter, const QPointF& center, qreal radius, const QColor& litColor, bool lit, bool dark)
{
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 190) : QColor(70, 62, 50, 190), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.2, radius + 1.2);

	if (lit)
	{
		QRadialGradient halo(center, radius * 3.2);
		halo.setColorAt(0.0, withAlpha(litColor, 110));
		halo.setColorAt(1.0, withAlpha(litColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, radius * 3.2, radius * 3.2);
	}

	QRadialGradient dome(center - QPointF(radius * 0.3, radius * 0.3), radius * 1.6);
	if (lit)
	{
		dome.setColorAt(0.0, litColor.lighter(150));
		dome.setColorAt(1.0, litColor.darker(125));
	}
	else
	{
		const QColor off = litColor.darker(330);
		dome.setColorAt(0.0, off.lighter(140));
		dome.setColorAt(1.0, off);
	}
	painter.setPen(Qt::NoPen);
	painter.setBrush(dome);
	painter.drawEllipse(center, radius, radius);
	painter.setBrush(QColor(255, 255, 255, lit ? 170 : (dark ? 28 : 60)));
	painter.drawEllipse(center - QPointF(radius * 0.35, radius * 0.35), radius * 0.3, radius * 0.3);
}

void paintBrushing(QPainter& painter, const QRectF& r, bool dark, uint seed)
{
	// The grain is constant along x, so it is rendered once into a 1px-wide
	// tile and stretched in a single blit. Painting it as hundreds of
	// full-width translucent hairlines cost ~160 ms per repaint of a
	// maximized QHD viewport (measured by --scroll-bench: 195 -> 32 ms per
	// wheel step across the whole rack scene) - the "scrolling lags when
	// maximized" field report. The hash now keys on the tile-local line
	// index, so the noise sequence differs from the old absolute-y hash;
	// same statistics, one-time gallery pixel shift.
	const int height = qRound(r.height());
	if (height < 4)
		return;
	const QString key = QStringLiteral("rack-brush:%1:%2:%3").arg(seed).arg(height).arg(dark ? 1 : 0);
	QPixmap tile;
	if (!QPixmapCache::find(key, &tile))
	{
		QImage image(1, height, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter tilePainter(&image);
		const int baseAlpha = dark ? 4 : 5;
		QColor ink = dark ? QColor(255, 255, 255) : QColor(96, 84, 64);
		for (int y = 2; y < height - 1; y += 2)
		{
			const uint h = (seed ^ uint(y * 7)) * 2654435761u;
			const bool polish = (h >> 8) % 11u == 0;
			ink.setAlpha(baseAlpha + int(h % 7u) + (polish ? 6 : 0));
			tilePainter.fillRect(0, y, 1, 1, ink);
		}
		tilePainter.end();
		tile = QPixmap::fromImage(image);
		QPixmapCache::insert(key, tile);
	}
	painter.drawPixmap(QRectF(r.left() + 2, r.top(), r.width() - 4, height),
		tile, QRectF(0, 0, 1, height));
}

void paintJack(QPainter& painter, const QPointF& center, bool dark)
{
	QRadialGradient flange(center - QPointF(1.4, 1.4), 7.5);
	if (dark)
	{
		flange.setColorAt(0.0, QColor(0xA8, 0xB1, 0xB8));
		flange.setColorAt(0.6, QColor(0x55, 0x5E, 0x64));
		flange.setColorAt(1.0, QColor(0x26, 0x2B, 0x2F));
	}
	else
	{
		flange.setColorAt(0.0, QColor(0xFF, 0xFF, 0xFC));
		flange.setColorAt(0.6, QColor(0xC0, 0xB9, 0xAA));
		flange.setColorAt(1.0, QColor(0x86, 0x7E, 0x6E));
	}
	painter.setPen(QPen(dark ? QColor(0, 0, 0, 210) : QColor(0x60, 0x58, 0x48), 1));
	painter.setBrush(flange);
	painter.drawEllipse(center, 4.6, 4.6);

	painter.setPen(QPen(QColor(0, 0, 0, 220), 1));
	painter.setBrush(QColor(8, 9, 10));
	painter.drawEllipse(center, 2.1, 2.1);

	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(255, 255, 255, dark ? 70 : 150));
	painter.drawEllipse(center + QPointF(-2.5, -2.7), 0.9, 0.9);
}

void paintGrain(QPainter& painter, const QRectF& r, const QColor& ink, uint seed)
{
	QColor line = ink;
	for (qreal y = r.top() + 2.0; y < r.bottom() - 1.0; y += 2.0)
	{
		const uint h = (seed ^ uint(qRound(y * 7.0))) * 2654435761u;
		const bool polish = (h >> 8) % 11u == 0;
		line.setAlpha(4 + int(h % 7u) + (polish ? 6 : 0));
		painter.setPen(QPen(line, 1));
		painter.drawLine(QPointF(r.left() + 2.0, y), QPointF(r.right() - 2.0, y));
	}
}
}
