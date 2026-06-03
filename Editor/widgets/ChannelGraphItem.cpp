/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <QGraphicsScene>
#include <QPainter>

#include "Editor/helpers/GUIHelper.h"
#include "ChannelGraphItem.h"

static const float margin = 5;

ChannelGraphItem::ChannelGraphItem(const QString& name)
	: name(name)
{
}

QRectF ChannelGraphItem::boundingRect() const
{
	if (cachedRect.isNull())
	{
		QFontMetrics metrics(scene()->font());
		int width = metrics.size(0, name).width();
		// we only use upper case characters and digits, so everything is above the baseline
		int height = metrics.ascent();
		// make it at least quadratic
		if (width < height)
			width = height;
		// shift by 0.5 to have sharp lines
		cachedRect.setLeft(0.5);
		cachedRect.setTop(0.5);
		cachedRect.setWidth(width + 2 * GUIHelper::scale(margin));
		cachedRect.setHeight(height + 2 * GUIHelper::scale(margin));
	}

	return cachedRect;
}

QString ChannelGraphItem::getName() const
{
	return name;
}

void ChannelGraphItem::paint(QPainter* painter, QColor color)
{
	QRectF rect = boundingRect();
	painter->setRenderHint(QPainter::Antialiasing, true);

	// Modern flat channel badge. The old style washed the top of every badge to
	// pure white (a hard, dated look) and forced black text; instead use a soft
	// same-hue sheen and pick the label colour from the badge luminance so the
	// name stays legible on any channel colour and in either theme. Shared by
	// the Channel filter selection and the studio Copy node graph.
	QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
	gradient.setColorAt(0, color.lighter(122));
	gradient.setColorAt(1, color);
	painter->setBrush(gradient);
	painter->setPen(QPen(color.darker(150), 1));
	const double radius = GUIHelper::scale(margin) + 1;
	painter->drawRoundedRect(rect, radius, radius);

	const double luminance = 0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue();
	painter->setPen(luminance > 145 ? QColor(0x18, 0x20, 0x2c) : QColor(0xf5, 0xf8, 0xfc));
	QFont labelFont = painter->font();
	labelFont.setBold(true);
	painter->setFont(labelFont);
	painter->drawText(rect, Qt::AlignCenter, name);
}
