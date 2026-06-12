/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2016  Jonas Thedering

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

#include <QSize>
#include <QString>
#include <QColor>
#include <QIcon>

#pragma once

class GUIHelper
{
public:
	static QSize scale(QSize size);
	static int scale(double pixel);
	static double scaleZoom(double zoom);
	static double invScale(int pixel);
	static double invScaleZoom(double zoom);
    static bool isDarkMode();
	// Render a monochrome resource icon (SVG silhouette) recoloured to the given
	// skin colour. The artwork's own colour is ignored: only its alpha mask is
	// kept, so the same icon adapts to any dark/light skin without per-theme
	// duplicate files. size is in logical pixels and is DPI-scaled internally.
	static QIcon tintedIcon(const QString& resource, const QColor& color, int size = 20);
	// Derive the application palette from the active skin's tokens and apply
	// it. Painted (non-QSS) widgets and native popups read these roles, so the
	// palette must follow the skin. Called at startup and by the offscreen
	// skin gallery whenever it switches skins.
	static void applySkinPalette();
	// User-configurable span for dB gain knobs (Preamp card, biquad gain dial):
	// a knob covers ±knobGainRange() dB, while direct text entry keeps each
	// command's full range and merely pegs the knob at its end. Stored under
	// interface/knobGainRange; clamped to [1, 100], default ±20 dB.
	static double knobGainRange();
	static void setKnobGainRange(double range);
};
