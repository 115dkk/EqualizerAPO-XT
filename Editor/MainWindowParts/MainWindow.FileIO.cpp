/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QTimer>
#include <QScrollArea>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "services/logging/Logging.h"
#include "Editor/helpers/CrashHandler.h"
#include "audio/ChannelLayout.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "version.h"
#include "FilterTable.h"
#include "ConfigFileCodec.h"
#include "MainWindow.h"
#include "Editor/MainWindowParts/OpenDocument.h"
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


void MainWindow::load(QString path)
{
	path = QDir::toNativeSeparators(path);

	forEachFilterTable([&](int i, FilterTable* filterTable) {
		if (filterTable->getConfigPath() == path)
		{
			ui->tabWidget->setCurrentIndex(i);
		}
	});
	if (FilterTable* filterTable = currentFilterTable();
		filterTable != nullptr && filterTable->getConfigPath() == path)
		return;

	QElapsedTimer timer;
	timer.start();

	CrashHandler::setBreadcrumb(L"opening file " + path.toStdWString());
	ConfigFileCodec::ReadResult readResult = ConfigFileCodec::readConfig(path);
	if (!readResult.ok)
	{
		QMessageBox::critical(this, tr("Error"), tr("Error while reading configuration file: %0").arg(readResult.errorMessage));
		return;
	}

	QList<QString> lines = readResult.lines;

	QFileInfo fileInfo(path);
	FilterTable* filterTable = addTab(fileInfo.fileName(), QDir::toNativeSeparators(fileInfo.absoluteFilePath()), path, lines);

	connect(filterTable, SIGNAL(linesChanged()), this, SLOT(linesChanged()));

	TraceF(L"Loading took %.1f ms", timer.nsecsElapsed() / 1e6);

	ui->tabWidget->setCurrentIndex(ui->tabWidget->count() - 1);
	updateDirtyStatus();

	RecentFiles::remember(recentFiles, path);
	updateRecentFiles();
}

DocumentSaveTransition MainWindow::saveDocument(OpenDocument& document,
	DocumentSaveReason reason, const QString& newPath)
{
	if (analysisProbeSaveStarted)
		analysisProbeSaveStarted();

	QElapsedTimer timer;
	timer.start();

	const QString path = DocumentSaveCore::targetPath(document.state, reason, newPath);
	const ConfigFileCodec::WriteResult writeResult = ConfigFileCodec::writeConfig(
		path, document.table->getLines());
	DocumentWriteResult coreWriteResult;
	coreWriteResult.opened = writeResult.opened;
	coreWriteResult.bytesWritten = writeResult.bytesWritten;
	coreWriteResult.totalBytes = writeResult.totalBytes;
	coreWriteResult.errorMessage = writeResult.errorMessage;
	const DocumentSaveTransition transition = DocumentSaveCore::transition(
		document.state, reason, newPath, coreWriteResult);

	if (transition.failure == DocumentSaveFailure::WriteError)
	{
		QMessageBox::critical(this, tr("Error"),
			tr("Error while writing configuration file: %0").arg(transition.errorMessage));
	}
	else if (transition.failure == DocumentSaveFailure::PartialWrite)
	{
		QMessageBox::critical(this, tr("Error"), tr("Only %0/%1 bytes have been written!")
			.arg(transition.bytesWritten).arg(transition.totalBytes));
	}
	else
	{
		// Path and size: a report that a save did not stick can then tell a write
		// that never ran from one that wrote other lines than expected (#459).
		TraceF(L"Saved %s (%lu bytes) in %.1f ms", path.toStdWString().c_str(),
			writeResult.bytesWritten, timer.nsecsElapsed() / 1e6);

		// Preserve the existing order: save() restarted analysis before its
		// callers updated dirty state or a Save As path. Other UI controls still
		// use startAnalysis() to coalesce their changes.
		if (transition.restartAnalysis)
		{
			if (analysisDebounceTimer != nullptr)
				analysisDebounceTimer->stop();
			executeStartAnalysis();
		}
	}

	if (document.state.title() != transition.title || document.state.path() != transition.path)
		document.state.savedAs(transition.title, transition.path);
	document.state.setDirty(transition.dirty);
	showDocumentTitle(document);
	updateDirtyStatus();
	return transition;
}

void MainWindow::flushPendingInstantSave(int tabIndex)
{
	OpenDocument* document = documentForTab(tabIndex);
	if (document == nullptr || !document->saveTimer.isActive())
		return;
	document->saveTimer.stop();
	if (!document->state.path().isEmpty())
		saveDocument(*document, DocumentSaveReason::InstantModeFlush);
	else
		updateDirtyStatus();
}

bool MainWindow::isEmpty()
{
	return ui->tabWidget->count() == 0;
}

bool MainWindow::shouldRestart()
{
	return restart;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	bool canceled = false;
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		if (!askForClose(i))
		{
			canceled = true;
			break;
		}
	}

	if (canceled)
	{
		event->ignore();
		restart = false;
		noSavePreferences = false;
		noSaveFilePreferences = false;
	}
	else
	{
		savePreferences();
	}
}
