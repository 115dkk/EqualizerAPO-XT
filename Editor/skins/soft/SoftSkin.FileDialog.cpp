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

	// The toolbar's pastel tiles carried into the dialog's navigation
	// row, with the same semantic tints: the movement pair rides
	// accent2 like undo/redo, the folder pair keeps actionOpen's warm
	// tint and actionNew's accent, and the view toggles stay muted so
	// they read as mode switches, not actions.
	const QColor card(tokens.card);
	// The default initializers keep cppcheck's uninitMemberVarNoCtor happy:
	// the QColor member gives this aggregate a non-trivial flavor its
	// heuristic mistakes for a constructor-less class.
	const struct { const char* name = nullptr; const char* resource = nullptr; QColor tile; } buttons[] = {
		{ "backButton", ":/icons/modern/nav-back.svg", mixColor(QColor(tokens.accent2), card, 0.15) },
		{ "forwardButton", ":/icons/modern/nav-forward.svg", mixColor(QColor(tokens.accent2), card, 0.15) },
		{ "toParentButton", ":/icons/modern/folder-up.svg", mixColor(QColor(tokens.warning), card, 0.15) },
		{ "newFolderButton", ":/icons/modern/folder-new.svg", mixColor(QColor(tokens.accent), card, 0.15) },
		{ "listModeButton", ":/icons/modern/view-list.svg", mixColor(QColor(tokens.mutedText), card, 0.15) },
		{ "detailModeButton", ":/icons/modern/view-detail.svg", mixColor(QColor(tokens.mutedText), card, 0.15) },
	};
	for (const auto& button : buttons)
	{
		QToolButton* toolButton = dialog->findChild<QToolButton*>(QLatin1String(button.name));
		if (toolButton != nullptr)
		{
			toolButton->setIcon(softTileIcon(QLatin1String(button.resource), button.tile));
			toolButton->setIconSize(GUIHelper::scale(QSize(22, 22)));
		}
	}
}
