/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MatrixSkin.h"

#include <QFileDialog>

#include "MatrixFileIcons.h"

void MatrixSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
		// Navigation keeps the shared stroke set on phosphor ink; the entry
		// pictograms switch to the panel's chamfered CRT glyphs. The faint
		// board grid behind the views comes from the skin sheet
		// (QFileDialog-scoped rules in matrix_*.qss).
		ISkin::styleFileDialog(dialog, tokens);
		if (dialog == nullptr)
			return;
		dialog->setIconProvider(matrixFileIconProvider(tokens));
	}
