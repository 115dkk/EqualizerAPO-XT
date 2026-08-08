#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QHash>

#include "FilterListModel.h"

// The mutation/selection semantics below mirror their FilterTable call sites
// (FilterTable.Clipboard.cpp / FilterTable.Model.cpp / FilterTable.Mouse.cpp)
// and live here so they can be unit-tested.

namespace
{
	using OwnedFilterListItem = std::unique_ptr<FilterListItem>;

	QList<FilterListItem*> buildItemView(const std::vector<OwnedFilterListItem>& ownedItems)
	{
		QList<FilterListItem*> result;
		result.reserve(static_cast<qsizetype>(ownedItems.size()));
		for (const auto& item : ownedItems)
			result.append(item.get());
		return result;
	}
}

QList<QString> FilterListModel::lines() const
{
	QList<QString> result;
	for (FilterListItem* item : itemList)
		result.append(item->text);

	return result;
}

void FilterListModel::setLines(const QList<QString>& lines)
{
	std::vector<OwnedFilterListItem> newOwnedItems;
	newOwnedItems.reserve(static_cast<size_t>(lines.size()));
	for (const QString& line : lines)
		newOwnedItems.push_back(std::make_unique<FilterListItem>(line));

	QList<FilterListItem*> newItemList = buildItemView(newOwnedItems);
	QSet<FilterListItem*> newSelectedSet;
	FilterListItem* newFocusedItem = newItemList.isEmpty() ? nullptr : newItemList[0];

	// All potentially-throwing work is complete. These swaps form one
	// no-throw commit, so the old document remains coherent on allocation
	// failure and is destroyed only after no raw observer references it.
	ownedItems.swap(newOwnedItems);
	itemList.swap(newItemList);
	selectedSet.swap(newSelectedSet);
	focusedItem = newFocusedItem;
	selectionStartItem = newFocusedItem;
}

FilterListItem* FilterListModel::addLine(const QString& line, const FilterListItem* before)
{
	auto newItem = std::make_unique<FilterListItem>(line);
	FilterListItem* result = newItem.get();
	int index = before == nullptr ? itemList.size() : itemList.indexOf(before);
	if (index < 0)
		index = itemList.size();

	QList<FilterListItem*> newItemList;
	newItemList.reserve(itemList.size() + 1);
	for (int i = 0; i <= itemList.size(); i++)
	{
		if (i == index)
			newItemList.append(result);
		if (i < itemList.size())
			newItemList.append(itemList[i]);
	}

	ownedItems.reserve(ownedItems.size() + 1);
	ownedItems.insert(ownedItems.begin() + index, std::move(newItem));
	itemList.swap(newItemList);

	return result;
}

bool FilterListModel::removeItem(FilterListItem* item)
{
	int index = itemList.indexOf(item);
	if (index == -1)
		return false;

	FilterListItem* replacement = nullptr;
	if (itemList.size() > 1)
		replacement = itemList[index + 1 < itemList.size() ? index + 1 : index - 1];

	QList<FilterListItem*> newItemList;
	newItemList.reserve(itemList.size() - 1);
	for (FilterListItem* existingItem : itemList)
	{
		if (existingItem != item)
			newItemList.append(existingItem);
	}

	QSet<FilterListItem*> newSelectedSet = selectedSet;
	if (newSelectedSet.remove(item) && replacement != nullptr)
		newSelectedSet.insert(replacement);
	FilterListItem* newFocusedItem = focusedItem == item ? replacement : focusedItem;
	FilterListItem* newSelectionStartItem = selectionStartItem == item ? replacement : selectionStartItem;

	itemList.swap(newItemList);
	selectedSet.swap(newSelectedSet);
	focusedItem = newFocusedItem;
	selectionStartItem = newSelectionStartItem;
	ownedItems.erase(ownedItems.begin() + index);
	return true;
}

void FilterListModel::removeItems(const QSet<FilterListItem*>& itemsToRemove)
{
	std::vector<FilterListItem*> removedItems;
	removedItems.reserve(ownedItems.size());
	QList<FilterListItem*> newItemList;
	newItemList.reserve(itemList.size());
	for (const auto& ownedItem : ownedItems)
	{
		FilterListItem* item = ownedItem.get();
		if (itemsToRemove.contains(item))
			removedItems.push_back(item);
		else
			newItemList.append(item);
	}
	if (removedItems.empty())
		return;

	QSet<FilterListItem*> newSelectedSet = selectedSet;
	for (FilterListItem* item : removedItems)
		newSelectedSet.remove(item);
	const auto wasRemoved = [&removedItems](FilterListItem* item) {
		return std::find(removedItems.begin(), removedItems.end(), item) != removedItems.end();
	};
	FilterListItem* newFocusedItem = wasRemoved(focusedItem) ? nullptr : focusedItem;
	FilterListItem* newSelectionStartItem = wasRemoved(selectionStartItem) ? nullptr : selectionStartItem;

	itemList.swap(newItemList);
	selectedSet.swap(newSelectedSet);
	focusedItem = newFocusedItem;
	selectionStartItem = newSelectionStartItem;
	ownedItems.erase(std::remove_if(ownedItems.begin(), ownedItems.end(), [&itemsToRemove](const auto& ownedItem) {
		return itemsToRemove.contains(ownedItem.get());
	}), ownedItems.end());
}

QList<FilterListItem*> FilterListModel::insertLines(const QStringList& lines, const QList<QVariantMap>& prefsList, int dropRow)
{
	dropRow = qBound(0, dropRow, itemList.size());
	std::vector<OwnedFilterListItem> newOwnedItems;
	newOwnedItems.reserve(static_cast<size_t>(lines.size()));
	QList<FilterListItem*> inserted;
	inserted.reserve(lines.size());
	for (int i = 0; i < lines.size(); i++)
	{
		auto ownedItem = std::make_unique<FilterListItem>(lines[i]);
		if (i < prefsList.size())
			ownedItem->prefs = prefsList[i];
		inserted.append(ownedItem.get());
		newOwnedItems.push_back(std::move(ownedItem));
	}

	QList<FilterListItem*> newItemList;
	newItemList.reserve(itemList.size() + inserted.size());
	for (int i = 0; i < dropRow; i++)
		newItemList.append(itemList[i]);
	for (FilterListItem* item : inserted)
		newItemList.append(item);
	for (int i = dropRow; i < itemList.size(); i++)
		newItemList.append(itemList[i]);

	QSet<FilterListItem*> newSelectedSet;
	newSelectedSet.reserve(inserted.size());
	for (FilterListItem* item : inserted)
		newSelectedSet.insert(item);
	FilterListItem* newFocusedItem = inserted.isEmpty() ? nullptr : inserted[0];

	ownedItems.reserve(ownedItems.size() + newOwnedItems.size());
	ownedItems.insert(ownedItems.begin() + dropRow,
		std::make_move_iterator(newOwnedItems.begin()),
		std::make_move_iterator(newOwnedItems.end()));
	itemList.swap(newItemList);
	selectedSet.swap(newSelectedSet);
	focusedItem = newFocusedItem;
	selectionStartItem = newFocusedItem;

	return inserted;
}

bool FilterListModel::moveItems(const QList<FilterListItem*>& itemsToMove, int dropRow)
{
	if (itemsToMove.isEmpty())
		return false;
	QSet<FilterListItem*> moving;
	moving.reserve(itemsToMove.size());
	for (FilterListItem* item : itemsToMove)
	{
		if (!itemList.contains(item))
			return false;
		moving.insert(item);
	}
	if (moving.size() != itemsToMove.size())
		return false;

	dropRow = qBound(0, dropRow, int(itemList.size()));

	// Compute the new order on the projection first: the moved items leave
	// their rows (shifting the insertion point left for every one that sat
	// above the drop row) and land as one block, in document order - the
	// same final order the drag-move's old insert-copies-then-remove-
	// originals splice produced.
	QList<FilterListItem*> newItemList;
	newItemList.reserve(itemList.size());
	QList<FilterListItem*> movedInOrder;
	movedInOrder.reserve(itemsToMove.size());
	int insertAt = dropRow;
	for (int i = 0; i < itemList.size(); i++)
	{
		if (moving.contains(itemList[i]))
		{
			if (i < dropRow)
				insertAt--;
			movedInOrder.append(itemList[i]);
		}
		else
			newItemList.append(itemList[i]);
	}
	for (int i = 0; i < movedInOrder.size(); i++)
		newItemList.insert(insertAt + i, movedInOrder[i]);

	// All potentially-throwing work (allocations) happens before the owner
	// vector is disturbed, so the no-throw commit pattern of the other
	// mutations holds.
	QHash<FilterListItem*, size_t> ownedIndex;
	ownedIndex.reserve(int(ownedItems.size()));
	for (size_t i = 0; i < ownedItems.size(); i++)
		ownedIndex.insert(ownedItems[i].get(), i);
	std::vector<OwnedFilterListItem> newOwnedItems;
	newOwnedItems.reserve(ownedItems.size());
	QSet<FilterListItem*> newSelectedSet = moving;
	FilterListItem* firstMoved = movedInOrder.first();

	for (FilterListItem* item : newItemList)
		newOwnedItems.push_back(std::move(ownedItems[ownedIndex.value(item)]));

	ownedItems.swap(newOwnedItems);
	itemList.swap(newItemList);
	selectedSet.swap(newSelectedSet);
	focusedItem = firstMoved;
	selectionStartItem = firstMoved;
	return true;
}

void FilterListModel::deleteSelected()
{
	QList<FilterListItem*> newItemList;
	// selectedSet is normally a subset of the document, but reserving the
	// current view size keeps this transaction safe even if a caller supplied
	// an external observer through the public selection Interface.
	newItemList.reserve(itemList.size());
	for (FilterListItem* item : itemList)
	{
		if (!selectedSet.contains(item))
			newItemList.append(item);
	}
	FilterListItem* newFocusedItem = selectedSet.contains(focusedItem) ? nullptr : focusedItem;
	FilterListItem* newSelectionStartItem = selectedSet.contains(selectionStartItem) ? nullptr : selectionStartItem;

	QSet<FilterListItem*> removedItems;
	removedItems.swap(selectedSet);
	itemList.swap(newItemList);
	focusedItem = newFocusedItem;
	selectionStartItem = newSelectionStartItem;
	ownedItems.erase(std::remove_if(ownedItems.begin(), ownedItems.end(), [&removedItems](const auto& ownedItem) {
		return removedItems.contains(ownedItem.get());
	}), ownedItems.end());
}

void FilterListModel::selectOnly(FilterListItem* item)
{
	QSet<FilterListItem*> newSelectedSet;
	newSelectedSet.insert(item);
	selectedSet.swap(newSelectedSet);
}

void FilterListModel::selectAll()
{
	QSet<FilterListItem*> newSelectedSet;
	newSelectedSet.reserve(itemList.size());
	for (FilterListItem* item : itemList)
		newSelectedSet.insert(item);
	selectedSet.swap(newSelectedSet);
}

void FilterListModel::selectRangeFromAnchor(const FilterListItem* target)
{
	int startRow = itemList.indexOf(selectionStartItem);
	int targetRow = itemList.indexOf(target);
	if (startRow == -1 || targetRow == -1)
		return;

	QSet<FilterListItem*> newSelectedSet;
	newSelectedSet.reserve(qAbs(targetRow - startRow) + 1);
	for (int i = qMin(startRow, targetRow); i <= qMax(startRow, targetRow); i++)
		newSelectedSet.insert(itemList[i]);
	selectedSet.swap(newSelectedSet);
}

int FilterListModel::firstSelectedIndex() const
{
	for (int i = 0; i < itemList.size(); i++)
	{
		if (selectedSet.contains(itemList[i]))
			return i;
	}

	return -1;
}

FilterListModel::CopyPayload FilterListModel::copyPayload() const
{
	CopyPayload payload;
	bool first = true;
	for (FilterListItem* item : itemList)
	{
		if (!selectedSet.contains(item))
			continue;

		if (first)
			first = false;
		else
			payload.text += '\n';
		payload.text += item->text;
		payload.prefsList.append(item->prefs);
	}

	return payload;
}
