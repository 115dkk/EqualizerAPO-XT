/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QAction>
#include <QToolBar>

#include "Editor/helpers/GUIHelper.h"
#include "Editor/skins/shared/SkinPaint.h"

// The toolbar is this skin's calm header band; the QSS sheets carry the
// band, the toggle, the pills and the stadium combos. The hook's share is
// the one thing QSS cannot express: the three file actions wear
// iOS-Settings-style rounded-square colour tiles - pastels mixed from
// existing tokens (accent blue for New, warning amber for the folder,
// success green for Save) under the shared stroke glyph, the same tile
// recipe as the picker's category tiles. Re-running the hook only calls
// setters, so skin/dark switches stay idempotent.
void SoftSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	if (toolBar == nullptr)
		return;

	toolBar->setIconSize(GUIHelper::scale(QSize(22, 22)));
	const QColor card(tokens.card);
	for (QAction* action : toolBar->actions())
	{
		if (action->objectName() == QStringLiteral("actionNew"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/file-new.svg"), mixColor(QColor(tokens.accent), card, 0.15)));
		else if (action->objectName() == QStringLiteral("actionOpen"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/folder-open.svg"), mixColor(QColor(tokens.warning), card, 0.15)));
		else if (action->objectName() == QStringLiteral("actionSave"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/save.svg"), mixColor(QColor(tokens.success), card, 0.15)));
		else if (action->objectName() == QStringLiteral("actionUndo"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/undo.svg"), mixColor(QColor(tokens.accent2), card, 0.15)));
		else if (action->objectName() == QStringLiteral("actionRedo"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/redo.svg"), mixColor(QColor(tokens.accent2), card, 0.15)));
	}
}
