/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct FilterPickerEntry
{
	QStringList path;
	QString name;
	QString line;
	QString description;
};

QString filterPickerSection(const FilterPickerEntry& entry);
bool filterPickerMatches(
	const FilterPickerEntry& entry,
	const QString& section,
	const QString& query);

struct FilterPickerMatch
{
	int originalIndex = -1;
	QString section;
};

// Shared picker state. Presentations consume this model but remain free to
// group and paint the matches in their own visual grammar.
class FilterPickerModel
{
public:
	void setEntries(const QList<FilterPickerEntry>& entries);
	const QList<FilterPickerEntry>& entries() const;

	void setQuery(const QString& query);
	const QString& query() const;
	QList<FilterPickerMatch> matches() const;
	int selectedIndex() const;
	bool selectIndex(int originalIndex);
	void moveSelection(int delta);

private:
	void selectFirstMatch();

	QList<FilterPickerEntry> entryList;
	QString searchQuery;
	int selection = -1;
};
