/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ActivatableListChrome.h"

// The persistent "add card" row at the end of the modern filter list. It
// replaces the legacy green-icon QToolBar in the card path. The widget owns
// all input handling - click,
// Space/Return, focus - and delegates every pixel to the active skin through
// ISkin::paintAddRow, so each skin answers the affordance in its own grammar
// (docs/skins/README.md, shared insertion contract). LegacyRows keeps the
// frozen toolbar and never constructs this widget.
class AddCardRow : public ActivatableListChrome
{
	Q_OBJECT

public:
	explicit AddCardRow(QWidget* parent = nullptr);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
};
