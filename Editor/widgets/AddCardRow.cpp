/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "AddCardRow.h"

#include <QPainter>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"

AddCardRow::AddCardRow(QWidget* parent)
	: ActivatableListChrome(parent)
{
	setObjectName(QStringLiteral("FilterAddRow"));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setToolTip(tr("Add filter"));
	setAccessibleName(tr("Add filter"));
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		updateGeometry();
		update();
	});
}

QSize AddCardRow::sizeHint() const
{
	// Mirror a card row's footprint: rowHeight plus the 4px outer margins the
	// cards carry above and below, so the ghost row sits on the same rhythm.
	return QSize(200, SkinManager::instance()->tokens().rowHeight + 8);
}

QSize AddCardRow::minimumSizeHint() const
{
	return QSize(0, SkinManager::instance()->tokens().rowHeight + 8);
}

void AddCardRow::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	ListChromeState state;
	state.hovered = isHovered();
	state.pressed = isPressed();
	state.focused = hasFocus();
	state.label = tr("Add filter");
	// The same horizontal inset as the card rows' outer layout (8px), so the
	// skin paints on the exact column the cards occupy.
	SkinManager::instance()->paintAddRow(painter, rect().adjusted(8, 4, -8, -4), state);
}
