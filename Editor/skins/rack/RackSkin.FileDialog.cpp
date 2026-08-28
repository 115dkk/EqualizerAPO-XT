/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RackSkin.h"

#include <QFileDialog>
#include <QStyle>
#include <QToolButton>

#include "RackFileIcons.h"

void RackSkin::styleFileDialog(QFileDialog* dialog, const SkinTokens& tokens) const
{
		// The shared stroke icons on the navigation row, then the transport
		// treatment: each nav button is tagged so the sheet raises it into a
		// machined cap like the main toolbar's keys (round-2 verdict: "위쪽
		// 툴바도 버튼처럼"). The attribute selector outranks the shared
		// fileDialogOverride padding reset, so the caps keep their own fit.
		ISkin::styleFileDialog(dialog, tokens);
		if (dialog == nullptr)
			return;
		const char* const navButtons[] = {
			"backButton", "forwardButton", "toParentButton",
			"newFolderButton", "listModeButton", "detailModeButton"
		};
		for (const char* name : navButtons)
		{
			QToolButton* button = dialog->findChild<QToolButton*>(QLatin1String(name));
			if (button == nullptr)
				continue;
			button->setProperty("rackTransport", true);
			button->style()->unpolish(button);
			button->style()->polish(button);
		}
		// The shelf objects behind the machine: skeuomorphic folder/file
		// pictograms.
		dialog->setIconProvider(rackFileIconProvider(tokens));
	}
