/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ActivatableListChrome.h"

// The hover-only insertion seam above the first card of the modern filter
// list. The header "+" inserts ABOVE its card (shared insertion contract,
// docs/skins/README.md); this seam is a second, direct entry point at the
// document's leading edge. It floats over the top margin of the first row,
// paints nothing at rest, and reveals a skin-drawn seam
// (ISkin::paintInsertSeam) while the cursor is inside. A click opens the
// filter picker and inserts the chosen line at index 0. Never constructed in
// LegacyRows mode.
class FilterInsertSeam : public ActivatableListChrome
{
	Q_OBJECT

public:
	explicit FilterInsertSeam(QWidget* parent = nullptr);

protected:
	void paintEvent(QPaintEvent* event) override;
};
