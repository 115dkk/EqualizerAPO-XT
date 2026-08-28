/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Soft Lab's "add filter" picker: a rounded settings-menu card - a pill
	search field over two-line rows led by pastel colour tiles.
	Constitution: docs/skins/soft.md ("필터 픽커" section).
*/

#pragma once

#include <QColor>
#include <QHash>
#include <QStringList>

#include "Editor/widgets/FilterPickerView.h"
#include "Editor/SkinTokens.h"

class QLineEdit;
class QListWidget;

class SoftFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit SoftFilterPickerView(const SkinTokens& tokens, QWidget* parent = nullptr);

	void galleryShowcase(GalleryShowcase kind) override;

	QSize sizeHint() const override;

protected:
	void entriesChanged() override;
	void paintEvent(QPaintEvent* event) override;

private:
	const SkinTokens skinTokens;
	void rebuildList();

	// Per-entry tile monograms, parallel to pickerEntries(); computed once per
	// catalog so a search never re-letters the tiles.
	QStringList entryMonograms;
	// Category -> pastel tile colour, assigned in catalog order so a category
	// keeps its hue however the search narrows the list.
	QHash<QString, QColor> sectionColors;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
	int listContentHeight = 0;
};
