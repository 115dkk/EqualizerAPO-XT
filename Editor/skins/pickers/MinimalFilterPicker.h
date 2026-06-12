/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	"Add filter" picker for the minimal skin (Precision Minimal): a terminal
	index. The control is all type, hairlines and alignment - a man page /
	BIOS menu rather than a dialog. One bare ">"-prefixed query line on top,
	a numbered mono index below (uppercase section captions with full-width
	hairline underlines, entries as single "NN  Name" lines) and a key legend
	at the bottom. Zero radius, no decoration; selection is an inverted text
	block, hover is exactly one background-value step.

	Keyboard is the soul: typing letters filters the index, typing digits
	jumps straight to that entry number, Up/Down move, Return inserts. Esc
	belongs to the popup host.

	Numbering (N3): numbers are page coordinates, assigned once from the
	resting index's display order (sections coalesced by first appearance),
	so the full listing counts 01..NN straight down the page. They never
	change afterwards - filtering hides lines but keeps their printed
	numbers, so "07" always jumps to the same template. A query that matches
	nothing answers with a NO MATCH line, the terminal's honest empty state.
*/

#pragma once

#include <functional>

#include <QList>
#include <QVector>

#include "Editor/widgets/FilterPickerView.h"

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
		QString number;       // zero-padded display-order number (N3); empty for captions
		QString text;         // entry name, or the uppercase caption text
		int entryIndex = -1;  // original index into the entries list; -1 = caption
	};

	explicit MinimalPickerIndexList(QWidget* parent = nullptr);

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
	explicit MinimalFilterPickerView(QWidget* parent = nullptr);

	void setEntries(const QList<FilterPickerEntry>& entries) override;
	void galleryShowcase(GalleryShowcase kind) override;
	QSize sizeHint() const override;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	QString sectionKey(const FilterPickerEntry& entry) const;
	void rebuildDisplayNumbers();
	void rebuildIndex();
	void moveSelection(int delta);
	void chooseCurrent();
	void ensureSelectionVisible();

	QList<FilterPickerEntry> allEntries;
	// N3 page coordinates: entry indices in resting display order, and each
	// entry's 1-based printed number. Assigned once per setEntries; immutable
	// while filtering so digit jumps stay stable.
	QVector<int> displayOrder;
	QVector<int> displayNumbers;
	QLineEdit* queryEdit = nullptr;
	QLabel* countLabel = nullptr;
	QScrollArea* scrollArea = nullptr;
	MinimalPickerIndexList* indexList = nullptr;
};
