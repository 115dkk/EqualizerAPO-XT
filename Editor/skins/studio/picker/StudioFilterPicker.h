/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio Glass "add filter" picker: a floating frosted-glass panel that
	paints its own stage, with a sunken-glass search field over a sectioned
	list.
	Constitution: docs/skins/studio.md ("필터 픽커" section).
*/

#pragma once

#include <QList>

#include "Editor/SkinTokens.h"
#include "Editor/widgets/FilterPickerView.h"

class QLineEdit;
class QListWidget;

class StudioFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit StudioFilterPickerView(const SkinTokens& tokens, QWidget* parent = nullptr);

	void galleryShowcase(GalleryShowcase kind) override;

protected:
	void entriesChanged() override;
	void paintEvent(QPaintEvent* event) override;

private:
	void rebuildList();

	SkinTokens skinTokens;
	bool dark = true;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
};
