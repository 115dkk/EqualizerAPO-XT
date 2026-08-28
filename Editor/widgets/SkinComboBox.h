/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	QComboBox with a DPI-aware sizing floor and a popup sized to its
	contents. The skins' QSS keeps a px floor for every combo box, but that
	floor cannot follow the user's font size; this subclass derives the
	control height from the font metrics, and widens the popup so long items
	(device names, channel configurations) are not elided to the closed
	control's width. Used by the toolbar dropdowns; promote a QComboBox to
	this class wherever the same floor is wanted.
*/

#pragma once

#include <QComboBox>

class SkinComboBox : public QComboBox
{
	Q_OBJECT

public:
	explicit SkinComboBox(QWidget* parent = nullptr);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;
	void showPopup() override;
};
