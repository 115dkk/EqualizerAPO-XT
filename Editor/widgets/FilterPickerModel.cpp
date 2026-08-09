/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "FilterPickerModel.h"

#include <QCoreApplication>
#include <QRegularExpression>

QString filterPickerSection(const FilterPickerEntry& entry)
{
	return entry.path.isEmpty()
		? QCoreApplication::translate("FilterPickerView", "General")
		: entry.path.join(QStringLiteral(" / "));
}

bool filterPickerMatches(
	const FilterPickerEntry& entry,
	const QString& section,
	const QString& query)
{
	const QString haystack = section + QLatin1Char(' ') + entry.name + QLatin1Char(' ') + entry.line;
	const QStringList terms = query.split(
		QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
	for (const QString& term : terms)
		if (!haystack.contains(term, Qt::CaseInsensitive))
			return false;
	return true;
}

void FilterPickerModel::setEntries(const QList<FilterPickerEntry>& entries)
{
	entryList = entries;
	selectFirstMatch();
}

const QList<FilterPickerEntry>& FilterPickerModel::entries() const
{
	return entryList;
}

void FilterPickerModel::setQuery(const QString& query)
{
	searchQuery = query;
	selectFirstMatch();
}

const QString& FilterPickerModel::query() const
{
	return searchQuery;
}

QList<FilterPickerMatch> FilterPickerModel::matches() const
{
	QList<FilterPickerMatch> result;
	for (int i = 0; i < entryList.size(); i++)
	{
		const QString section = filterPickerSection(entryList[i]);
		if (filterPickerMatches(entryList[i], section, searchQuery))
			result.append({ i, section });
	}
	return result;
}

int FilterPickerModel::selectedIndex() const
{
	return selection;
}

bool FilterPickerModel::selectIndex(int originalIndex)
{
	for (const FilterPickerMatch& match : matches())
	{
		if (match.originalIndex == originalIndex)
		{
			selection = originalIndex;
			return true;
		}
	}
	return false;
}

void FilterPickerModel::moveSelection(int delta)
{
	const QList<FilterPickerMatch> matchingEntries = matches();
	if (matchingEntries.isEmpty())
	{
		selection = -1;
		return;
	}

	int position = -1;
	for (int i = 0; i < matchingEntries.size(); i++)
	{
		if (matchingEntries[i].originalIndex == selection)
		{
			position = i;
			break;
		}
	}
	position = position < 0
		? 0
		: qBound(0, position + delta, matchingEntries.size() - 1);
	selection = matchingEntries[position].originalIndex;
}

void FilterPickerModel::selectFirstMatch()
{
	const QList<FilterPickerMatch> matchingEntries = matches();
	selection = matchingEntries.isEmpty() ? -1 : matchingEntries.first().originalIndex;
}
