/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QFileDialog>

void MinimalSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
	// Navigation keeps the shared stroke set in body ink; the entry
	// pictograms switch to the terminal's hairline provider.
	ISkin::styleFileDialog(dialog, tokens);
	if (dialog == nullptr)
		return;
	installFileIconProvider(dialog, tokens);
}
