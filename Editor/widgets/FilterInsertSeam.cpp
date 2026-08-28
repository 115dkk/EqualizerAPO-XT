/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FilterInsertSeam.h"

#include <QPainter>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"

FilterInsertSeam::FilterInsertSeam(QWidget* parent)
	: ActivatableListChrome(parent)
{
	setObjectName(QStringLiteral("FilterInsertSeam"));
	// Invisible at rest: the widget only paints while hovered, so it must not
	// contribute any background of its own.
	setAttribute(Qt::WA_NoSystemBackground, true);
	setToolTip(tr("Insert filter at the top"));
	setAccessibleName(tr("Insert filter at the top"));
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		update();
	});
}

void FilterInsertSeam::paintEvent(QPaintEvent*)
{
	if (!isHovered() && !isPressed() && !hasFocus())
		return;

	QPainter painter(this);
	ListChromeState state;
	state.hovered = isHovered();
	state.pressed = isPressed();
	state.focused = hasFocus();
	state.label = tr("Insert filter at the top");
	SkinManager::instance()->paintInsertSeam(painter, rect().adjusted(8, 0, -8, 0), state);
}
