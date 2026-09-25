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
#include "audio/ChannelLayout.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "version.h"
#include "FilterTable.h"
#include "ConfigFileCodec.h"
#include "MainWindow.h"
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

	recentFiles.removeAll(path);
	recentFiles.prepend(path);
	if (recentFiles.size() > 10)
		recentFiles.removeLast();
	updateRecentFiles();
}

bool MainWindow::save(FilterTable* filterTable, QString path)
{
	QElapsedTimer timer;
	timer.start();

	QList<QString> lines = filterTable->getLines();

	ConfigFileCodec::WriteResult writeResult = ConfigFileCodec::writeConfig(path, lines);
	if (!writeResult.opened)
	{
		QMessageBox::critical(this, tr("Error"), tr("Error while writing configuration file: %0").arg(writeResult.errorMessage));
		return false;
	}
	if (writeResult.bytesWritten != writeResult.totalBytes)
	{
		QMessageBox::critical(this, tr("Error"), tr("Only %0/%1 bytes have been written!").arg(writeResult.bytesWritten).arg(writeResult.totalBytes));
		return false;
	}

	TraceF(L"Saving took %.1f ms", timer.nsecsElapsed() / 1e6);

	startAnalysis();
	updateDirtyStatus();
	return true;
}

void MainWindow::setTabDirty(int tabIndex, bool dirty)
{
	if (tabIndex < 0 || tabIndex >= ui->tabWidget->count())
		return;
	const QString tabText = ui->tabWidget->tabText(tabIndex);
	if (dirty && !tabText.endsWith('*'))
		ui->tabWidget->setTabText(tabIndex, tabText + '*');
	else if (!dirty && tabText.endsWith('*'))
		ui->tabWidget->setTabText(tabIndex, tabText.left(tabText.length() - 1));
}

void MainWindow::flushPendingInstantSave(int tabIndex)
{
	FilterTable* filterTable = filterTableForTab(tabIndex);
	if (filterTable == nullptr)
		return;
	QTimer* timer = filterTable->findChild<QTimer*>(QStringLiteral("__instantModeSaveTimer"), Qt::FindDirectChildrenOnly);
	if (timer == nullptr || !timer->isActive())
		return;
	timer->stop();
	const QString path = filterTable->getConfigPath();
	if (path.length() > 0 && !save(filterTable, path))
		setTabDirty(tabIndex, true);
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
