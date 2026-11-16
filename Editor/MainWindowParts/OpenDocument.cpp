/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "OpenDocument.h"

#include <QDir>
#include <QFileInfo>

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

QString DocumentSaveCore::targetPath(const DocumentState& document, DocumentSaveReason reason,
	const QString& newPath)
{
	return reason == DocumentSaveReason::NewPath ? newPath : document.path();
}

DocumentSaveTransition DocumentSaveCore::transition(const DocumentState& document,
	DocumentSaveReason reason, const QString& newPath, const DocumentWriteResult& writeResult)
{
	DocumentSaveTransition result;
	result.title = document.title();
	result.path = document.path();
	result.dirty = document.dirty();
	result.errorMessage = writeResult.errorMessage;

	if (!writeResult.opened)
		result.failure = DocumentSaveFailure::WriteError;

	if (result.failure != DocumentSaveFailure::None)
	{
		// Instant mode normally leaves a tab clean while its debounce waits. A
		// failed write has to put the close prompt back; explicit saves preserve
		// the dirty state they were called with.
		if (reason == DocumentSaveReason::InstantModeFlush)
			result.dirty = true;
		return result;
	}

	result.restartAnalysis = true;
	switch (reason)
	{
	case DocumentSaveReason::CurrentPath:
		result.dirty = false;
		break;
	case DocumentSaveReason::NewPath:
	{
		const QFileInfo fileInfo(newPath);
		result.title = fileInfo.fileName();
		result.path = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
		result.dirty = false;
		break;
	}
	case DocumentSaveReason::InstantModeFlush:
		// The write covers the whole document, so a dirty flag left by an
		// earlier failed instant save is cleared: the file now matches, and the
		// close prompt would ask about nothing (audit #467 TD-44).
		result.dirty = false;
		break;
	}
	return result;
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
