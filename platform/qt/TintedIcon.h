/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

// A monochrome glyph recoloured to a skin's ink. Shared by the Editor
// (GUIHelper forwards here) and the Device Selector, whose caption strip
// tints its close glyph the same way.
namespace TintedIcon
{
// The glyph at resource, rendered at size x size and refilled with color. The
// icon also carries a faded variant for the Disabled mode, so a disabled
// action reads as disabled from the glyph itself.
QIcon make(const QString& resource, const QColor& color, int size);

// The same pixmap with its alpha cut to roughly a third.
QPixmap faded(const QPixmap& pixmap);
}
