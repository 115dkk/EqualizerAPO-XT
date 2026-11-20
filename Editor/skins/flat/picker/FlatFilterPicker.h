/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Stock Flat's "add filter" picker: a rounded settings-menu card - a pill
	search field over two-line rows led by pastel colour tiles.
	Constitution: docs/skins/flat.md ("필터 픽커" section).
*/

#pragma once

#include <QColor>
#include <QHash>
#include <QStringList>

#include "Editor/widgets/FilterPickerView.h"
#include "Editor/SkinTokens.h"

class QLineEdit;
class QListWidget;

class FlatFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit FlatFilterPickerView(const SkinTokens& tokens, QWidget* parent = nullptr);

	void galleryShowcase(GalleryShowcase kind) override;

	QSize sizeHint() const override;

protected:
	void entriesChanged() override;
	void paintEvent(QPaintEvent* event) override;

private:
	const SkinTokens skinTokens;
	void rebuildList();

	// Per-entry monograms, parallel to pickerEntries(); computed once per
	// catalog so a search never re-letters the rows.
	QStringList entryMonograms;
	QLineEdit* searchEdit = nullptr;
	QListWidget* listWidget = nullptr;
	int listContentHeight = 0;
};
