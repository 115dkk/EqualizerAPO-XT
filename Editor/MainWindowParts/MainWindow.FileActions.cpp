/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <sstream>
#include <QApplication>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
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
#include "MainWindow.h"
#include "Editor/MainWindowParts/OpenDocument.h"
#include "Editor/widgets/cards/VSTPluginSession.h"
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


void MainWindow::on_actionOpen_triggered()
{
	QString path;
	if (FilterTable* filterTable = currentFilterTable())
	{
		if (filterTable->getConfigPath().length() > 0)
		{
			QFileInfo fileInfo(filterTable->getConfigPath());
			path = fileInfo.absolutePath();
		}
	}
	if (path.length() == 0)
		path = configDir.absolutePath();

	QFileDialog dialog(this, tr("Open file"), path, "*.txt");
	dialog.setFileMode(QFileDialog::ExistingFiles);
	dialog.setNameFilter(tr("E-APO configurations (*.txt)"));
	GUIHelper::prepareFileDialog(dialog);

	if (dialog.exec() == QDialog::Accepted)
	{
		for (QString file : dialog.selectedFiles())
			load(file);
	}
}

void MainWindow::on_actionSave_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;

	commitPendingEdits();

	if (filterTable->getConfigPath().length() == 0)
	{
		ui->actionSaveAs->trigger();
	}
	else
	{
		if (save(filterTable, filterTable->getConfigPath()))
			setTabDirty(ui->tabWidget->currentIndex(), false);
		updateDirtyStatus();
	}
}

void MainWindow::on_actionSaveAs_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;

	commitPendingEdits();

	QString path;
	QString filename;
	if (filterTable->getConfigPath().length() == 0)
	{
		path = configDir.absolutePath();
	}
	else
	{
		QFileInfo fileInfo(filterTable->getConfigPath());
		path = fileInfo.absolutePath();
		filename = fileInfo.fileName();
	}
	QFileDialog dialog(this, tr("Save file as"), path, "*.txt");
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	dialog.setNameFilter(tr("E-APO configurations (*.txt)"));
	dialog.setDefaultSuffix(".txt");
	GUIHelper::prepareFileDialog(dialog);
	if (filename.length() > 0)
		dialog.selectFile(filename);

	if (dialog.exec() == QDialog::Accepted)
	{
		QString savePath = dialog.selectedFiles().at(0);
		// A failed write keeps the tab on its old path and unsaved.
		if (!save(filterTable, savePath))
		{
			updateDirtyStatus();
			return;
		}
		filterTable->setConfigPath(QDir::toNativeSeparators(savePath));

		QFileInfo fileInfo(savePath);
		if (OpenDocument* document = documentForTab(ui->tabWidget->currentIndex()))
		{
			document->state.savedAs(fileInfo.fileName(), QDir::toNativeSeparators(fileInfo.absoluteFilePath()));
			showDocumentTitle(*document);
		}
		updateDirtyStatus();
	}
}

void MainWindow::on_actionNew_triggered()
{
	FilterTable* filterTable = addTab(tr("Unsaved"), "", "", QList<QString>());

	connect(filterTable, SIGNAL(linesChanged()), this, SLOT(linesChanged()));
	ui->tabWidget->setCurrentIndex(ui->tabWidget->count() - 1);
	updateDirtyStatus();
}

void MainWindow::recentFileSelected()
{
	QAction* action = qobject_cast<QAction*>(sender());
	load(action->text());
}


void MainWindow::commitPendingEdits()
{
	// A value typed into a card's field or raw-line editor reaches the
	// document only when that editor commits it: on Enter, or when the focus
	// leaves it. Ctrl+S and the window's close button leave the focus where
	// it is, so a save wrote the file without the value, and the close found
	// nothing unsaved and committed it only while the window went away (#459).
	// Taking the focus makes the same commit a click elsewhere makes; an
	// editor that is still there gets it back.
	QWidget* focus = QApplication::focusWidget();
	if (focus != nullptr && isAncestorOf(focus))
	{
		// The commit can rebuild the card and delete its editor.
		const QPointer<QWidget> editor(focus);
		focus->clearFocus();
		if (!editor.isNull() && editor->isVisible())
			editor->setFocus(Qt::OtherFocusReason);
	}
	// An embedded plug-in panel is otherwise read back only when the UI is
	// idle and at most once a second.
	VSTPluginSession::readBackEmbeddedPanels();
}

bool MainWindow::askForClose(int tabIndex)
{
	// Before the instant-mode flush: a commit here starts that mode's save
	// debounce, which the flush then writes at once.
	commitPendingEdits();
	flushPendingInstantSave(tabIndex);
	bool discarded = false;
	const OpenDocument* document = documentForTab(tabIndex);
	if (document != nullptr && document->state.dirty())
	{
		ui->tabWidget->setCurrentIndex(tabIndex);
		QString configPath = document->state.path();
		QMessageBox messageBox;
		messageBox.setWindowTitle(tr("Unsaved changes"));
		messageBox.setText(tr("The configuration file %0 has unsaved changes.").arg(configPath));
		messageBox.setInformativeText(tr("Do you want to save the changes before closing the file?"));
		messageBox.setIcon(QMessageBox::Question);
		messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		messageBox.setDefaultButton(QMessageBox::Save);
		messageBox.setEscapeButton(QMessageBox::Cancel);
		messageBox.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
		int result = messageBox.exec();
		TraceF(L"Close prompt for %s answered %s", configPath.toStdWString().c_str(),
			result == QMessageBox::Save ? L"save" : result == QMessageBox::Discard ? L"discard" : L"cancel");

		switch (result)
		{
		case QMessageBox::Save:
			ui->actionSave->trigger();
			if (document->state.dirty())
				// saving was canceled
				return false;
			break;
		case QMessageBox::Discard:
			discarded = true;
			break;
		case QMessageBox::Cancel:
			return false;
		}
	}

	if (!discarded && !noSaveFilePreferences)
	{
		if (FilterTable* filterTable = filterTableForTab(tabIndex))
			filterTable->savePreferences();
	}

	return true;
}

