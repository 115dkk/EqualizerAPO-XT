/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
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
	ISkin::styleFileDialog(dialog, recededTokens(tokens));
}

QAbstractFileIconProvider* StudioSkin::createFileIconProvider(const SkinTokens& tokens) const
{
	// The folder/file pictograms are engravings in the dialog chrome's
	// receded ink.
	static StudioFileIconProvider iconProvider;
	iconProvider.updateTokens(recededTokens(tokens));
	return &iconProvider;
}
