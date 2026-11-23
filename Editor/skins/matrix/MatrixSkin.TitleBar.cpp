/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MatrixSkin.h"

#include "Editor/skins/shared/TitleBarChrome.h"

// The painter is shared with the Device Selector's captions.
void MatrixSkin::paintTitleBarChrome(QPainter& painter, const QRect& rect, const SkinTokens& tokens) const
{
	TitleBarChrome::paintMatrix(painter, rect, tokens);
}
