/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The Matrix caption: graph-paper rules and the baseline above the strip's foot.
	A free function so the Device Selector, which has no skin classes,
	paints the same caption: MatrixSkin::paintTitleBarChrome forwards here and
	the Device Selector's matrix device skin calls it. Uses only header-only
	helpers and code the Device Selector already compiles.
*/

#pragma once

#include "Editor/SkinTokens.h"

class QPainter;
class QRect;

namespace MatrixTitleBarChrome
{
void paint(QPainter& painter, const QRect& rect, const SkinTokens& tokens);
}
