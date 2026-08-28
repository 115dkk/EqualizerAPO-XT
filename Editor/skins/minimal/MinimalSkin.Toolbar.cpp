/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "MinimalSkin.h"

#include <QToolBar>

// The neutral default keeps the shared stroke icons on the actions so the
// File menu (which shares the QActions) stays modern; the toolbar buttons
// themselves drop the pictures and show the command words instead
// (precision_*.qss dresses them). Both calls set absolute state, so
// re-running on every skin/dark switch is idempotent.
void MinimalSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	if (toolBar == nullptr)
		return;
	ISkin::styleMainToolbar(toolBar, tokens);
	toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
}
