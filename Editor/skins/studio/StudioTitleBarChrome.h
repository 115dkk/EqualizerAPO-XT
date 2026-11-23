/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The Studio caption: the lighter top edge with the accent bloom at its centre.
	A free function so the Device Selector, which has no skin classes,
	paints the same caption: StudioSkin::paintTitleBarChrome forwards here and
	the Device Selector's studio device skin calls it. Uses only header-only
	helpers and code the Device Selector already compiles.
*/

#pragma once

#include "Editor/SkinTokens.h"

class QPainter;
class QRect;

namespace StudioTitleBarChrome
{
void paint(QPainter& painter, const QRect& rect, const SkinTokens& tokens);
}
