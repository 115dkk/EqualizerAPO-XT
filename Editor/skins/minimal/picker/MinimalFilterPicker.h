/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	"Add filter" picker for the minimal skin (Precision Minimal): a numbered
	terminal index - a bare ">" query line, a mono index list and a key
	legend. Letters filter, digits jump to a printed number, Esc belongs to
	the popup host.
	Constitution: docs/skins/minimal.md ("필터 픽커" section).
*/

#pragma once

#include <functional>

#include <QList>
#include <QVector>

#include "Editor/widgets/FilterPickerView.h"
#include "Editor/SkinTokens.h"

class QLabel;
class QLineEdit;
class QScrollArea;

// The painted index body. It only paints rows and hit-tests clicks; all
// keyboard logic and filtering live in MinimalFilterPickerView. Painting by
// hand (instead of a QListWidget) keeps the mono number column, the caption
// underlines and the inverted selection block on exact 1px terms.
class MinimalPickerIndexList : public QWidget
{
public:
	struct Row
	{
		QString number;       // zero-padded display-order number; empty for captions
		QString text;         // entry name, or the uppercase caption text
		int entryIndex = -1;  // original index into the entries list; -1 = caption
	};

	explicit MinimalPickerIndexList(const SkinTokens& tokens, QWidget* parent = nullptr);

	void setRows(const QList<Row>& rows);
	const QList<Row>& rows() const { return rowList; }

	void setSelectedEntry(int entryIndex);
	int selectedEntry() const { return selectedEntryIndex; }
	int rowOfEntry(int entryIndex) const;
	QRect rowRect(int row) const;

	// Offscreen gallery staging: hover the first line that is not the
	// selection block, so one shot shows both vocabularies (the inverted
	// cursor and the one-step hover) side by side.
	void hoverFirstEntryForGallery();

	// One click inserts (dropdown semantics, same as the neutral picker).
	std::function<void(int entryIndex)> onEntryActivated;

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	const SkinTokens skinTokens;
	int rowAt(const QPoint& pos) const;

	QList<Row> rowList;
	QVector<int> rowTops;
	int contentHeight = 0;
	int selectedEntryIndex = -1;
	int hoverRow = -1;
};

class MinimalFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit MinimalFilterPickerView(const SkinTokens& tokens, QWidget* parent = nullptr);

	void galleryShowcase(GalleryShowcase kind) override;
	QSize sizeHint() const override;

protected:
	void entriesChanged() override;
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	const SkinTokens skinTokens;
	QString sectionKey(const FilterPickerEntry& entry) const;
	void rebuildDisplayNumbers();
	void rebuildIndex();
	void moveSelection(int delta);
	void chooseCurrent();
	void ensureSelectionVisible();

	// Page coordinates: entry indices in resting display order, and each
	// entry's 1-based printed number. Assigned once per setEntries; immutable
	// while filtering so digit jumps stay stable.
	QVector<int> displayOrder;
	QVector<int> displayNumbers;
	QLineEdit* queryEdit = nullptr;
	QLabel* countLabel = nullptr;
	QScrollArea* scrollArea = nullptr;
	MinimalPickerIndexList* indexList = nullptr;
};
