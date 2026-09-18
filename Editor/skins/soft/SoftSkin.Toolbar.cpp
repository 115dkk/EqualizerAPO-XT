/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SoftSkin.h"

#include <QAction>
#include <QPointer>
#include <QToolBar>
#include <QToolButton>

#include "Editor/helpers/GUIHelper.h"

// The toolbar is this skin's calm header band; the QSS sheets carry the
// band, the toggle, the pills and the well combos. The hook's share is
// what QSS cannot express: the file actions wear stroke glyphs in the
// body ink with their names beside them, the accepted mockup's toolbar
// ("New", "Open", "Save" as words, the width of a wide window doing the
// carrying). Undo and redo keep the bare arrows: with all five names the
// band outgrew the default 1024px window and pushed the device selector
// into the extension menu, and the arrows are the two glyphs nobody
// needs a word for. No colour tiles - the concept swap keeps colour for
// what is chosen or needs attention, and a toolbar button is neither.
// SkinManager::styleMainToolbar resets the button style to icon-only
// before every skin hook, so the other four skins never inherit the
// labels. Re-running the hook only calls setters, so skin/dark switches
// stay idempotent.
void SoftSkin::styleMainToolbar(QToolBar* toolBar, const SkinTokens& tokens) const
{
	if (toolBar == nullptr)
		return;

	toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toolBar->setIconSize(GUIHelper::scale(QSize(18, 18)));
	const QColor ink(tokens.text);
	for (QAction* action : toolBar->actions())
	{
		// A labelled button must never elide its name: when the band is
		// narrower than its contents, QToolBarLayout squeezes buttons to
		// their minimum before it overflows anything into the extension
		// menu, and a squeezed label ("Sa...") says nothing. Pinning the
		// minimum to the styled size hint makes the layout overflow whole
		// items instead. Queued, so the pin is measured after this skin's
		// sheet has polished the button (the hook runs inside the skin
		// switch). SkinManager::styleMainToolbar clears the pin before the
		// next skin's hook.
		QPointer<QToolButton> button = qobject_cast<QToolButton*>(toolBar->widgetForAction(action));
		const bool history = action->objectName() == QStringLiteral("actionUndo")
			|| action->objectName() == QStringLiteral("actionRedo");
		if (button != nullptr && history)
			button->setToolButtonStyle(Qt::ToolButtonIconOnly);
		if (button != nullptr && !history && !action->text().isEmpty())
		{
			QMetaObject::invokeMethod(button, [button]() {
				if (button != nullptr && button->toolButtonStyle() == Qt::ToolButtonTextBesideIcon)
					button->setMinimumWidth(button->sizeHint().width());
			}, Qt::QueuedConnection);
		}
		if (action->objectName() == QStringLiteral("actionNew"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/file-new.svg"), Qt::transparent, ink));
		else if (action->objectName() == QStringLiteral("actionOpen"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/folder-open.svg"), Qt::transparent, ink));
		else if (action->objectName() == QStringLiteral("actionSave"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/save.svg"), Qt::transparent, ink));
		else if (action->objectName() == QStringLiteral("actionUndo"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/undo.svg"), Qt::transparent, ink));
		else if (action->objectName() == QStringLiteral("actionRedo"))
			action->setIcon(softTileIcon(QStringLiteral(":/icons/modern/redo.svg"), Qt::transparent, ink));
	}
}
