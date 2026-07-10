#include <QtAlgorithms>

#include "FilterListModel.h"

// The mutation/selection semantics below mirror their FilterTable call sites
// (FilterTable.Clipboard.cpp / FilterTable.Model.cpp / FilterTable.Mouse.cpp)
// and live here so they can be unit-tested.

FilterListModel::~FilterListModel()
{
	qDeleteAll(itemList);
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
	qDeleteAll(itemList);
	itemList.clear();
	// FilterTable::setLines never cleared the selection set, leaving it full
	// of pointers into the deleted document; clear it so a reused address in
	// the new document can never appear pre-selected.
	selectedSet.clear();

	for (const QString& line : lines)
		itemList.append(new FilterListItem(line));

	if (!itemList.isEmpty())
	{
		focusedItem = itemList[0];
		selectionStartItem = itemList[0];
	}
	else
	{
		focusedItem = nullptr;
		selectionStartItem = nullptr;
	}
}

FilterListItem* FilterListModel::addLine(const QString& line, const FilterListItem* before)
{
	FilterListItem* newItem = new FilterListItem(line);

	if (before != nullptr)
	{
		int index = itemList.indexOf(before);
		itemList.insert(index, newItem);
	}
	else
	{
		itemList.append(newItem);
	}

	return newItem;
}

bool FilterListModel::removeItem(FilterListItem* item)
{
	int index = itemList.indexOf(item);
	if (index == -1)
		return false;

	itemList.removeAt(index);
	FilterListItem* replacement = nullptr;
	if (!itemList.isEmpty())
		replacement = itemList[qMin(index, int(itemList.size()) - 1)];

	if (selectedSet.remove(item) && replacement != nullptr)
		selectedSet.insert(replacement);
	if (focusedItem == item)
		focusedItem = replacement;
	if (selectionStartItem == item)
		selectionStartItem = replacement;

	delete item;
	return true;
}

void FilterListModel::removeItems(const QSet<FilterListItem*>& itemsToRemove)
{
	for (FilterListItem* item : itemsToRemove)
	{
		itemList.removeOne(item);
		if (focusedItem == item)
			focusedItem = nullptr;
		if (selectionStartItem == item)
			selectionStartItem = nullptr;
		selectedSet.remove(item);
		delete item;
	}
}

QList<FilterListItem*> FilterListModel::insertLines(const QStringList& lines, const QList<QVariantMap>& prefsList, int dropRow)
{
	QList<FilterListItem*> inserted;

	selectedSet.clear();
	focusedItem = nullptr;
	selectionStartItem = nullptr;
	for (int i = 0; i < lines.size(); i++)
	{
		FilterListItem* item = new FilterListItem(lines[i]);
		if (i < prefsList.size())
			item->prefs = prefsList[i];
		selectedSet.insert(item);
		itemList.insert(dropRow++, item);
		if (focusedItem == nullptr)
		{
			focusedItem = item;
			selectionStartItem = item;
		}
		inserted.append(item);
	}

	return inserted;
}

void FilterListModel::deleteSelected()
{
	QList<FilterListItem*> newItems;
	for (FilterListItem* item : itemList)
	{
		if (selectedSet.contains(item))
		{
			if (item == focusedItem)
				focusedItem = nullptr;
			if (item == selectionStartItem)
				selectionStartItem = nullptr;
			delete item;
		}
		else
		{
			newItems.append(item);
		}
	}
	selectedSet.clear();
	itemList = newItems;
}

void FilterListModel::selectOnly(FilterListItem* item)
{
	selectedSet.clear();
	selectedSet.insert(item);
}

void FilterListModel::selectAll()
{
	selectedSet.clear();
	for (FilterListItem* item : itemList)
		selectedSet.insert(item);
}

void FilterListModel::selectRangeFromAnchor(const FilterListItem* target)
{
	int startRow = itemList.indexOf(selectionStartItem);
	int targetRow = itemList.indexOf(target);
	if (startRow == -1 || targetRow == -1)
		return;

	selectedSet.clear();
	for (int i = qMin(startRow, targetRow); i <= qMax(startRow, targetRow); i++)
		selectedSet.insert(itemList[i]);
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
