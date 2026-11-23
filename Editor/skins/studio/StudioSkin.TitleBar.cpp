/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "StudioSkin.h"

#include "StudioTitleBarChrome.h"

// The painter is shared with the Device Selector's captions.
void StudioSkin::paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const
{
	StudioTitleBarChrome::paint(painter, rect, tokens);
}
