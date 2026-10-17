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
#include <QDebug>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QStyle>
#include <QScrollArea>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QTimer>

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
#include "SkinManager.h"
#include "ui_MainWindow.h"

using std::find;
using std::list;
using std::set;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::vector;
using std::wstring;


void MainWindow::linesChanged()
{
	FilterTable* filterTable = qobject_cast<FilterTable*>(sender());
	if (filterTable == nullptr)
	{
		qWarning() << "linesChanged from unexpected sender" << sender();
		return;
	}

	// Every document edit lands here (undo capture rides the same signal), so
	// this keeps the Edit-menu undo/redo enabled states current.
	updateUndoRedoActions();

	OpenDocument* document = documentForTable(filterTable);
	if (document == nullptr)
		return;

	// Instant mode saves through the document's debounce instead of marking
	// it unsaved.
	if (document->state.edited(instantModeCheckBox->isChecked(), filterTable->getConfigPath().length() > 0))
	{
		document->saveTimer.start(DocumentState::instantSaveDebounceMs);
		updateDirtyStatus();
		return;
	}

	showDocumentTitle(*document);
	updateDirtyStatus();
}

void MainWindow::updateDirtyStatus()
{
	if (dirtyStatusLabel == nullptr)
		return;

	const OpenDocument* document = documentForTab(ui->tabWidget->currentIndex());
	const bool dirty = document != nullptr && document->state.dirty();
	dirtyStatusLabel->setText(dirty ? tr("Unsaved changes") : tr("Saved"));
	// The badge's look is owned by the skins: every sheet styles
	// QLabel#DirtyStatusBadge and its [dirty="true"] variant in its own
	// grammar. Setting an inline stylesheet here would override all of that,
	// so only the dynamic property + repolish are used.
	dirtyStatusLabel->setProperty("dirty", dirty);
	dirtyStatusLabel->style()->unpolish(dirtyStatusLabel);
	dirtyStatusLabel->style()->polish(dirtyStatusLabel);
	dirtyStatusLabel->update();
}

bool MainWindow::on_tabWidget_tabCloseRequested(int index)
{
	if (askForClose(index))
	{
		if (FilterTable* filterTable = filterTableForTab(index))
		{
			RecentFiles::remember(recentFiles, filterTable->getConfigPath());
			updateRecentFiles();
		}

		QWidget* page = ui->tabWidget->widget(index);
		ui->tabWidget->removeTab(index);
		std::erase_if(documents, [page](const std::unique_ptr<OpenDocument>& document) {
			return document->page == page;
		});
		if (page != nullptr)
			page->deleteLater();
		updateDirtyStatus();
	}
	return true;
}


// Document-level undo/redo for the active tab. While a text field has focus,
// its own edit shortcuts win (QLineEdit and friends accept the ShortcutOverride
// for Ctrl+Z/Ctrl+Y), so these actions only fire against the filter list
// itself. An empty history is a silent no-op, matching the other edit actions'
// tolerance for inapplicable states.
void MainWindow::on_actionUndo_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;

	filterTable->undo();
	updateUndoRedoActions();
}

void MainWindow::on_actionRedo_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;

	filterTable->redo();
	updateUndoRedoActions();
}

void MainWindow::updateUndoRedoActions()
{
	FilterTable* filterTable = currentFilterTable();
	ui->actionUndo->setEnabled(filterTable != nullptr && filterTable->canUndo());
	ui->actionRedo->setEnabled(filterTable != nullptr && filterTable->canRedo());
}

void MainWindow::on_actionCut_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;
	filterTable->cut();
}

void MainWindow::on_actionCopy_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;
	filterTable->copy();
}

void MainWindow::on_actionPaste_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;
	filterTable->paste();
}

void MainWindow::on_actionDelete_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;
	filterTable->deleteSelectedLines();
}

void MainWindow::on_actionSelectAll_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;
	filterTable->selectAll();
}

