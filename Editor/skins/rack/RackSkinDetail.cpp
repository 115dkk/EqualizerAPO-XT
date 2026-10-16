/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RackSkinDetail.h"
#include "Editor/skins/rack/RackPalette.h"

#include <QPainter>
#include <QPixmapCache>
#include <QRadialGradient>
#include <QtMath>

#include "Editor/skins/shared/SkinPaint.h"

namespace
{
// The brushing grain's per-line ink alpha: a hash of the seed and the line's
// key varies the density, and about one line in eleven is a brighter polish
// line. Every brushed surface of the rack (Editor and DeviceSelector) draws
// its lines with this one recipe.
int grainLineAlpha(uint seed, uint lineKey, int baseAlpha)
{
	const uint h = (seed ^ lineKey) * 2654435761u;
	const bool polish = (h >> 8) % 11u == 0;
	return baseAlpha + int(h % 7u) + (polish ? 6 : 0);
}
}

namespace RackSkinDetail
{
void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, bool dark)
{
	engraveText(painter, rect, flags, text, body, RackPalette::EngraveRelief(dark));
}

void engraveText(QPainter& painter, const QRectF& rect, int flags, const QString& text, const QColor& body, const QColor& relief)
{
	painter.setPen(relief);
	painter.drawText(rect.translated(0, 1), flags, text);
	painter.setPen(body);
	painter.drawText(rect, flags, text);
}

void paintScrew(QPainter& painter, const QPointF& center, qreal radius, qreal slotDegrees, bool dark)
{
	QRadialGradient body(center - QPointF(radius * 0.35, radius * 0.35), radius * 2.1);
	if (dark)
	{
		body.setColorAt(0.0, RackPalette::ScrewHighlight.dark);
		body.setColorAt(0.55, RackPalette::ScrewBody.dark);
		body.setColorAt(1.0, RackPalette::ScrewEdge.dark);
	}
	else
	{
		body.setColorAt(0.0, RackPalette::ScrewHighlight.light);
		body.setColorAt(0.55, RackPalette::ScrewBody.light);
		body.setColorAt(1.0, RackPalette::ScrewEdge.light);
	}
	painter.setPen(QPen(RackPalette::ScrewRim(dark), 1));
	painter.setBrush(body);
	painter.drawEllipse(center, radius, radius);

	const qreal rad = qDegreesToRadians(slotDegrees);
	const QPointF dir(qCos(rad), qSin(rad));
	const QPointF a = center - dir * (radius - 1.2);
	const QPointF b = center + dir * (radius - 1.2);
	painter.setPen(QPen(RackPalette::ScrewSlot(dark), 1.4, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a, b);
	painter.setPen(QPen(RackPalette::light(dark ? 60 : 170), 0.8, Qt::SolidLine, Qt::RoundCap));
	painter.drawLine(a + QPointF(0, 1), b + QPointF(0, 1));
}

void paintLed(QPainter& painter, const QPointF& center, qreal radius, const QColor& litColor, bool lit, bool dark)
{
	paintLed(painter, center, radius, radius * 3.2, litColor, lit, dark);
}

void paintLed(QPainter& painter, const QPointF& center, qreal radius, qreal haloRadius, const QColor& litColor, bool lit, bool dark)
{
	painter.setPen(QPen(RackPalette::LedBezel(dark), 1));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(center, radius + 1.2, radius + 1.2);

	if (lit)
	{
		QRadialGradient halo(center, haloRadius);
		halo.setColorAt(0.0, withAlpha(litColor, 110));
		halo.setColorAt(1.0, withAlpha(litColor, 0));
		painter.setPen(Qt::NoPen);
		painter.setBrush(halo);
		painter.drawEllipse(center, haloRadius, haloRadius);
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
	painter.setBrush(RackPalette::light(lit ? 170 : (dark ? 28 : 60)));
	painter.drawEllipse(center - QPointF(radius * 0.35, radius * 0.35), radius * 0.3, radius * 0.3);
}

void paintBrushing(QPainter& painter, const QRectF& r, bool dark, uint seed)
{
	paintBrushing(painter, r, RackPalette::BrushingGrain(dark), dark ? 4 : 5, seed);
}

void paintBrushing(QPainter& painter, const QRectF& r, const QColor& ink, int baseAlpha, uint seed)
{
	// The grain is constant along x, so it is rendered once into a 1px-wide
	// tile and stretched in a single blit. Painting it as hundreds of
	// full-width translucent hairlines cost ~160 ms per repaint of a
	// maximized QHD viewport (measured by --scroll-bench: 195 -> 32 ms per
	// wheel step across the whole rack scene) - the "scrolling lags when
	// maximized" field report. The hash now keys on the tile-local line
	// index, so the noise sequence differs from the old absolute-y hash;
	// same statistics, one-time gallery pixel shift. The module picker and
	// DeviceSelector's rack painter drew their own absolute-y loops until
	// audit #422 S6 moved them here and took the same shift.
	const int height = qRound(r.height());
	if (height < 4)
		return;
	// The ink and the base alpha are part of the key: DeviceSelector draws
	// both finishes in one process with the same seeds and heights.
	const QString key = QStringLiteral("rack-brush:%1:%2:%3:%4")
		.arg(seed).arg(height).arg(ink.rgb(), 0, 16).arg(baseAlpha);
	QPixmap tile;
	if (!QPixmapCache::find(key, &tile))
	{
		QImage image(1, height, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter tilePainter(&image);
		QColor line = ink;
		for (int y = 2; y < height - 1; y += 2)
		{
			line.setAlpha(grainLineAlpha(seed, uint(y * 7), baseAlpha));
			tilePainter.fillRect(0, y, 1, 1, line);
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
		flange.setColorAt(0.0, RackPalette::JackFlangeHighlight.dark);
		flange.setColorAt(0.6, RackPalette::JackFlangeMid.dark);
		flange.setColorAt(1.0, RackPalette::JackFlangeEdge.dark);
	}
	else
	{
		flange.setColorAt(0.0, RackPalette::JackFlangeHighlight.light);
		flange.setColorAt(0.6, RackPalette::JackFlangeMid.light);
		flange.setColorAt(1.0, RackPalette::JackFlangeEdge.light);
	}
	painter.setPen(QPen(RackPalette::JackFlangeRim(dark), 1));
	painter.setBrush(flange);
	painter.drawEllipse(center, 4.6, 4.6);

	painter.setPen(QPen(RackPalette::shadow(220), 1));
	painter.setBrush(RackPalette::JackBore);
	painter.drawEllipse(center, 2.1, 2.1);

	painter.setPen(Qt::NoPen);
	painter.setBrush(RackPalette::light(dark ? 70 : 150));
	painter.drawEllipse(center + QPointF(-2.5, -2.7), 0.9, 0.9);
}

void paintGrain(QPainter& painter, const QRectF& r, const QColor& ink, uint seed)
{
	// Still keyed on the absolute y (the button caps' grain), so its lines
	// are not those of paintBrushing's tile.
	QColor line = ink;
	for (qreal y = r.top() + 2.0; y < r.bottom() - 1.0; y += 2.0)
	{
		line.setAlpha(grainLineAlpha(seed, uint(qRound(y * 7.0)), 4));
		painter.setPen(QPen(line, 1));
		painter.drawLine(QPointF(r.left() + 2.0, y), QPointF(r.right() - 2.0, y));
	}
}
}
