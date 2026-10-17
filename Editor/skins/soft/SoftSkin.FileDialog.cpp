/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SoftSkin.h"

#include <QFileDialog>
#include <QToolButton>

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
	// One tile per button, in fileDialogNavButtons() order: back, forward,
	// to parent, new folder, list mode, detail mode.
	const QColor tiles[] = {
		mixColor(QColor(tokens.accent2), card, 0.15),
		mixColor(QColor(tokens.accent2), card, 0.15),
		mixColor(QColor(tokens.warning), card, 0.15),
		mixColor(QColor(tokens.accent), card, 0.15),
		mixColor(QColor(tokens.mutedText), card, 0.15),
		mixColor(QColor(tokens.mutedText), card, 0.15),
	};
	constexpr auto buttons = fileDialogNavButtons();
	static_assert(std::size(tiles) == buttons.size());
	for (std::size_t i = 0; i < buttons.size(); i++)
	{
		QToolButton* toolButton = dialog->findChild<QToolButton*>(QLatin1String(buttons[i].name));
		if (toolButton != nullptr)
		{
			toolButton->setIcon(softTileIcon(QLatin1String(buttons[i].resource), tiles[i]));
			toolButton->setIconSize(QSize(22, 22));
		}
	}
	// The entry pictograms answer in the same tile language, smaller;
	// SkinManager installs them from createFileIconProvider.
}
