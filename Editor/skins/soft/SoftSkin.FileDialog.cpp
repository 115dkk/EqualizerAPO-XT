/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QFileDialog>
#include <QToolButton>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/shared/SkinPaint.h"

void SoftSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
	if (dialog == nullptr)
		return;

	// The dialog's navigation row keeps rounded-square tiles (the
	// buttons have no room for names), but in the paper grammar: a well
	// tile under the stroke glyph in body ink, the same for every button.
	// No semantic tints - a navigation button is neither chosen nor in
	// need of attention.
	const QColor tile(tokens.surfaceSunken);
	const QColor ink(tokens.text);
	const char* const buttons[][2] = {
		{ "backButton", ":/icons/modern/nav-back.svg" },
		{ "forwardButton", ":/icons/modern/nav-forward.svg" },
		{ "toParentButton", ":/icons/modern/folder-up.svg" },
		{ "newFolderButton", ":/icons/modern/folder-new.svg" },
		{ "listModeButton", ":/icons/modern/view-list.svg" },
		{ "detailModeButton", ":/icons/modern/view-detail.svg" },
	};
	for (const auto& button : buttons)
	{
		QToolButton* toolButton = dialog->findChild<QToolButton*>(QLatin1String(button[0]));
		if (toolButton != nullptr)
		{
			toolButton->setIcon(softTileIcon(QLatin1String(button[1]), tile, ink));
			toolButton->setIconSize(GUIHelper::scale(QSize(22, 22)));
		}
	}
}
