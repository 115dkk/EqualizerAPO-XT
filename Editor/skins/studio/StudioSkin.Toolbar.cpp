/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StudioSkin.h"

#include <QAction>
#include <QToolBar>

#include "Editor/helpers/GUIHelper.h"

void StudioSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	if (toolBar == nullptr)
		return;

	const QColor text(tokens.text);
	const QColor muted(tokens.mutedText);
	const QColor ink((muted.red() + text.red()) / 2,
		(muted.green() + text.green()) / 2,
		(muted.blue() + text.blue()) / 2);
	toolBar->setIconSize(GUIHelper::scale(QSize(18, 18)));
	for (QAction* action : toolBar->actions())
	{
		if (action->objectName() == QStringLiteral("actionNew"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/file-new.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionOpen"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionSave"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/save.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionUndo"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/undo.svg"), ink, 18));
		else if (action->objectName() == QStringLiteral("actionRedo"))
			action->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/redo.svg"), ink, 18));
	}
}
