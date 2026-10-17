/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "OpenDocument.h"

#include <utility>

DocumentState::DocumentState(QString title, QString path)
	: titleText(std::move(title)), filePath(std::move(path))
{
}

const QString& DocumentState::title() const
{
	return titleText;
}

const QString& DocumentState::path() const
{
	return filePath;
}

bool DocumentState::dirty() const
{
	return unsaved;
}

QString DocumentState::tabText() const
{
	return unsaved ? titleText + QLatin1Char('*') : titleText;
}

void DocumentState::setDirty(bool dirty)
{
	unsaved = dirty;
}

bool DocumentState::edited(bool instantMode, bool hasFile)
{
	if (instantMode && hasFile)
		return true;
	unsaved = true;
	return false;
}

void DocumentState::savedAs(QString newTitle, QString newPath)
{
	titleText = std::move(newTitle);
	filePath = std::move(newPath);
	unsaved = false;
}

void RecentFiles::remember(QStringList& recentFiles, const QString& path)
{
	// A document that was never saved (a closed "Unsaved" tab) has no file
	// to reopen.
	if (path.isEmpty())
		return;
	recentFiles.removeAll(path);
	recentFiles.prepend(path);
	if (recentFiles.size() > 10)
		recentFiles.removeLast();
}

OpenDocument::OpenDocument(QWidget* page, FilterTable* table, DocumentState state)
	: page(page), table(table), state(std::move(state))
{
	saveTimer.setSingleShot(true);
}
