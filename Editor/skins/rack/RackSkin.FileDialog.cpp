/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "RackSkin.h"

#include <QFileDialog>
#include <QToolButton>

#include "Editor/skins/shared/SkinPaint.h"

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
	for (const SkinFileDialogNavButton& navButton : fileDialogNavButtons())
	{
		QToolButton* button = dialog->findChild<QToolButton*>(QLatin1String(navButton.name));
		if (button == nullptr)
			continue;
		button->setProperty("rackTransport", true);
		repolish(button);
	}
}
