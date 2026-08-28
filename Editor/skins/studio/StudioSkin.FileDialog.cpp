/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioSkin.h"
#include "StudioFileIcons.h"

#include <QFileDialog>

void StudioSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
	if (dialog == nullptr)
		return;
	// The shared stroke set in the toolbar's half-muted ink, so the
	// dialog chrome recedes behind the file data the same way the main
	// toolbar recedes behind the cards.
	const QColor text(tokens.text);
	const QColor muted(tokens.mutedText);
	const QColor ink((muted.red() + text.red()) / 2,
		(muted.green() + text.green()) / 2,
		(muted.blue() + text.blue()) / 2);
	SkinTokens recededTokens = tokens;
	recededTokens.text = ink.name(QColor::HexRgb);
	ISkin::styleFileDialog(dialog, recededTokens);
	// The folder/file pictograms are engravings in the same receded ink.
	static StudioFileIconProvider iconProvider;
	iconProvider.updateTokens(recededTokens);
	dialog->setIconProvider(&iconProvider);
}
