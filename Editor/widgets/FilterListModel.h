#pragma once

#include <memory>
#include <vector>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

class IFilterGUI;

// One line of the loaded config file. The gui pointer is stored opaquely for
// the widget layer (FilterTable/FilterCardRow own and dereference it); the
// model never touches it, so this header stays QtCore-only and links into
// EditorLogicTests.
struct FilterListItem
{
	FilterListItem()
	{
	}

	FilterListItem(const QString& text)
	{
		this->text = text;
	}

	QString text;
	QVariantMap prefs;
	IFilterGUI* gui = nullptr;
};

// The widget-free document/selection model behind FilterTable; the mutation
// and selection logic is unit-testable without a QWidget.
//
// Ownership: the model owns every FilterListItem it hands out. Items are
// deleted when they are removed (removeItem/removeItems/deleteSelected),
// replaced (setLines) or when the model is destroyed. Callers must not delete
// items themselves and must drop raw pointers after any removing mutation.
class FilterListModel
{
public:
	FilterListModel() = default;
	~FilterListModel() = default;

	FilterListModel(const FilterListModel&) = delete;
	FilterListModel& operator=(const FilterListModel&) = delete;

	// --- document ---

	const QList<FilterListItem*>& items() const
	{
		return itemList;
	}

	// The document text, one entry per item.
	QList<QString> lines() const;

	// Replaces the whole document. Focus and the selection anchor move to the
	// first line (or null when empty) and the selection set is cleared, so it
	// can never hold dangling pointers into the deleted document.
	void setLines(const QList<QString>& lines);

	// Inserts a new line before the given item, or appends when before is
	// null. Returns the created item. Selection state is not changed.
	FilterListItem* addLine(const QString& line, const FilterListItem* before = nullptr);

	// Removes and deletes one item. Selection, focus and the anchor move to
	// the neighbouring item (the one now at the removed index, or the last).
	// Returns false when the item is not part of the document.
	bool removeItem(FilterListItem* item);

	// Removes and deletes the given items without picking a replacement
	// selection (the internal drag-move semantics: the drop already selected
	// the inserted copies).
	void removeItems(const QSet<FilterListItem*>& itemsToRemove);

	// Inserts the lines at dropRow (0..items().size()) with prefs aligned by
	// index (missing entries stay empty). The inserted items replace the
	// selection; focus and the anchor land on the first inserted line.
	// Returns the inserted items in document order.
	QList<FilterListItem*> insertLines(const QStringList& lines, const QList<QVariantMap>& prefsList, int dropRow);

	// Moves the given document items so they sit as one contiguous block
	// (kept in document order) before the item currently at dropRow
	// (pre-move indexing; items().size() appends, out-of-range values are
	// clamped). The final order matches insertLines-copies-then-removeItems,
	// but the items themselves survive: pointers, prefs and gui stay valid.
	// The moved items replace the selection; focus and the anchor land on
	// the first moved item. Returns false (mutating nothing) when the list
	// is empty, holds duplicates or holds an item outside the document.
	bool moveItems(const QList<FilterListItem*>& itemsToMove, int dropRow);

	// Removes and deletes exactly the selected items, clears the selection and
	// drops focus/anchor if they pointed at a deleted item.
	void deleteSelected();

	// --- selection ---

	const QSet<FilterListItem*>& selected() const
	{
		return selectedSet;
	}

	FilterListItem* focused() const
	{
		return focusedItem;
	}

	FilterListItem* selectionStart() const
	{
		return selectionStartItem;
	}

	void setFocused(FilterListItem* item)
	{
		focusedItem = item;
	}

	void setSelectionStart(FilterListItem* item)
	{
		selectionStartItem = item;
	}

	bool isSelected(FilterListItem* item) const
	{
		return selectedSet.contains(item);
	}

	void select(FilterListItem* item)
	{
		selectedSet.insert(item);
	}

	// Returns whether the item had been selected (the Ctrl-click toggle needs
	// the old state).
	bool deselect(FilterListItem* item)
	{
		return selectedSet.remove(item);
	}

	// Makes the item the only selected one.
	void selectOnly(FilterListItem* item);

	void clearSelection()
	{
		selectedSet.clear();
	}

	void selectAll();

	// Shift-click/Shift-arrow range selection: replaces the selection with the
	// contiguous run between the current anchor (selectionStart) and target.
	// Leaves the selection untouched when the anchor or target is not part of
	// the document.
	void selectRangeFromAnchor(const FilterListItem* target);

	// Document index of the topmost selected item, or -1 when nothing is
	// selected (paste uses it as the insertion row).
	int firstSelectedIndex() const;

	// --- clipboard payload ---

	// The selected items in document order: their text joined with '\n' and
	// one prefs map per line. Feeds cut/copy and the drag mime data.
	struct CopyPayload
	{
		QString text;
		QList<QVariantMap> prefsList;
	};

	CopyPayload copyPayload() const;

private:
	std::vector<std::unique_ptr<FilterListItem>> ownedItems;
	// Stable, non-owning projection retained as the readable caller Interface.
	// Mutations prepare its replacement before swapping owner and view state.
	QList<FilterListItem*> itemList;
	QSet<FilterListItem*> selectedSet;
	FilterListItem* focusedItem = nullptr;
	FilterListItem* selectionStartItem = nullptr;
};
