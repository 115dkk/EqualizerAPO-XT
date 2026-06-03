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

#include <QPainter>

#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "ChannelFilterGUIScene.h"
#include "ChannelFilterGUIChannelItem.h"

ChannelFilterGUIChannelItem::ChannelFilterGUIChannelItem(const QString& name)
	: ChannelGraphItem(name)
{
	setFlag(ItemIsSelectable);
}

void ChannelFilterGUIChannelItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
	// Give each channel its own colour (shared with the Copy routing views) so
	// the selection reads at a glance: selected channels are filled with the
	// vivid channel colour, unselected ones are dimmed and desaturated.
	QColor channelColor(CopyRoutingAdapter::channelColor(getName()));

	QColor color;
	if (isSelected())
	{
		color = channelColor;
	}
	else
	{
		int h, s, v, a;
		channelColor.getHsv(&h, &s, &v, &a);
		color.setHsv(h < 0 ? 0 : h, static_cast<int>(s * 0.35), static_cast<int>(v * 0.5), a);
	}

	ChannelGraphItem::paint(painter, color);
}
