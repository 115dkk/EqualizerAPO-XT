/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	One open configuration tab's document (audit #422 D5): the title and path
	its tab shows, whether it has unsaved changes, and its instant-mode save
	debounce. MainWindow used to read these back from the tab text (a
	trailing '*'), the tab tooltip and a timer child found by name; the tab
	now only shows what this holds. DocumentState and the recent-files rule
	have no widgets, so EditorLogicTests pins them.
*/

#pragma once

#include <QString>
#include <QStringList>
#include <QTimer>

class FilterTable;
class QWidget;

class DocumentState
{
public:
	// Edits coalesce for this long before an instant-mode save writes the
	// file: dragging a knob or typing a value would otherwise write the file
	// once per change.
	static constexpr int instantSaveDebounceMs = 200;

	DocumentState(QString title, QString path);

	const QString& title() const;
	// The file as the tab's tooltip shows it; empty for a new document.
	const QString& path() const;
	bool dirty() const;
	// What the tab says: the title, with a trailing '*' while the document
	// has unsaved changes.
	QString tabText() const;

	void setDirty(bool dirty);
	// An edit to the document. With instant mode on and a file to write it
	// is saved after the debounce (returns true) and the tab shows no '*';
	// otherwise it is marked unsaved (returns false).
	bool edited(bool instantMode, bool hasFile);
	// Saved under a new name: the tab shows that name and path, and no
	// unsaved changes.
	void savedAs(QString newTitle, QString newPath);

private:
	QString titleText;
	QString filePath;
	bool unsaved = false;
};

namespace RecentFiles
{
// Puts path first in the list, without duplicates, keeping the ten most
// recent. An empty path (a document that was never saved) is ignored.
void remember(QStringList& recentFiles, const QString& path);
}

// What MainWindow keeps per tab. The page is the tab widget's page (the
// scroll area around table), which finds the document again after the user
// moved the tab.
struct OpenDocument
{
	OpenDocument(QWidget* page, FilterTable* table, DocumentState state);

	QWidget* page = nullptr;
	FilterTable* table = nullptr;
	DocumentState state;
	// Single shot; started by every edit in instant mode.
	QTimer saveTimer;
};
