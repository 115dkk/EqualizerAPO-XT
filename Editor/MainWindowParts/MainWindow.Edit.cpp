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

#include "text/StringHelper.h"
#include "services/logging/LogHelper.h"
#include "audio/ChannelHelper.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "version.h"
#include "FilterTable.h"
#include "MainWindow.h"
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

	if (instantModeCheckBox->isChecked())
	{
		QString configPath = filterTable->getConfigPath();
		if (configPath.length() > 0)
		{
			// Debounce instant-mode saves: dragging a knob or typing in a value
			// would otherwise trigger a full file write per change, so coalesce
			// all changes within a short window into a single save.
			static constexpr int kSaveDebounceMs = 200;
			const QString timerObjectName = QStringLiteral("__instantModeSaveTimer");
			QTimer* timer = filterTable->findChild<QTimer*>(timerObjectName, Qt::FindDirectChildrenOnly);
			if (!timer)
			{
				timer = new QTimer(filterTable);
				timer->setObjectName(timerObjectName);
				timer->setSingleShot(true);
				// Capturing filterTable is safe: the timer is its child, so the
				// timer cannot outlive the FilterTable. Re-resolve the path on
				// fire because the FilterTable's config path can change while
				// the debounce window is pending.
				connect(timer, &QTimer::timeout, this, [this, filterTable]() {
					QString currentPath = filterTable->getConfigPath();
					if (currentPath.length() > 0)
					{
						save(filterTable, currentPath);
						updateDirtyStatus();
					}
				});
			}
			timer->start(kSaveDebounceMs);
			updateDirtyStatus();
			return;
		}
	}

	int tabIndex = -1;
	forEachFilterTable([&](int i, FilterTable* candidate) {
		if (candidate == filterTable)
			tabIndex = i;
	});
	if (tabIndex < 0)
		return;
	QString tabText = ui->tabWidget->tabText(tabIndex);
	if (!tabText.endsWith('*'))
	{
		tabText += '*';
		ui->tabWidget->setTabText(tabIndex, tabText);
	}
	updateDirtyStatus();
}

void MainWindow::updateDirtyStatus()
{
	if (dirtyStatusLabel == nullptr)
		return;

	const int index = ui->tabWidget->currentIndex();
	const bool dirty = index >= 0 && ui->tabWidget->tabText(index).endsWith('*');
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
			QString path = filterTable->getConfigPath();
			recentFiles.removeAll(path);
			recentFiles.prepend(path);
			if (recentFiles.size() > 10)
				recentFiles.removeLast();
			updateRecentFiles();
		}

		QWidget* page = ui->tabWidget->widget(index);
		ui->tabWidget->removeTab(index);
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

