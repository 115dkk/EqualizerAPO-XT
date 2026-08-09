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
