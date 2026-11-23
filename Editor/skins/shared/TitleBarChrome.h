/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The painted chrome a skin lays over the caption strip's QSS ground. The
	Editor's skin classes forward their ISkin::paintTitleBarChrome here, and
	the Device Selector, which has no skin classes, calls paint() with its
	skin id, so a dialog's caption looks the same in both programs. Free of
	the skin classes on purpose: everything it uses is header-only or already
	compiled into the Device Selector (RackSkinDetail).
*/

#pragma once

#include <QString>

#include "Editor/SkinTokens.h"

class QPainter;
class QRect;

namespace TitleBarChrome
{
// Studio: the lighter top edge with the accent bloom at its centre.
void paintStudio(QPainter& painter, const QRect& rect, const SkinTokens& tokens);
// Rack: the brushed top panel, the caption ear and the rail screws.
void paintRack(QPainter& painter, const QRect& rect, const SkinTokens& tokens);
// Matrix: graph-paper rules and the baseline above the strip's foot.
void paintMatrix(QPainter& painter, const QRect& rect, const SkinTokens& tokens);

// The chrome of the skin with this roster id. Minimal and Stock Flat have
// none (their QSS ground is the whole caption), and nor does an id outside
// the roster.
void paint(const QString& skinId, QPainter& painter, const QRect& rect, const SkinTokens& tokens);
}
