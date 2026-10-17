/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "StudioSkin.h"

#include <QColor>
#include <QToolBar>

SkinTokens StudioSkin::recededTokens(const SkinTokens& tokens)
{
	const QColor text(tokens.text);
	const QColor muted(tokens.mutedText);
	const QColor ink((muted.red() + text.red()) / 2,
		(muted.green() + text.green()) / 2,
		(muted.blue() + text.blue()) / 2);
	SkinTokens receded = tokens;
	receded.text = ink.name(QColor::HexRgb);
	return receded;
}

void StudioSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	// The shared stroke set in half-muted ink, so the toolbar recedes
	// behind the cards.
	ISkin::styleMainToolbar(toolBar, recededTokens(tokens));
}
