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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
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

	if (instantModeCheckBox->isChecked())
	{
		QString configPath = filterTable->getConfigPath();
		if (configPath.length() > 0)
		{
			save(filterTable, configPath);
			updateDirtyStatus();
			return;
		}
	}

	int tabIndex = -1;
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
		if (scrollArea->widget() == filterTable)
		{
			tabIndex = i;
			break;
		}
	}
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
	dirtyStatusLabel->setProperty("dirty", dirty);
	const SkinTokens& tokens = SkinManager::instance()->tokens();
	dirtyStatusLabel->setStyleSheet(QStringLiteral("QLabel#DirtyStatusBadge { background: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 4px 10px; font-weight: 700; }")
		.arg(dirty ? tokens.warning : tokens.surfaceRaised,
			dirty ? QStringLiteral("#111111") : tokens.text,
			dirty ? tokens.warning : tokens.border));
	dirtyStatusLabel->style()->unpolish(dirtyStatusLabel);
	dirtyStatusLabel->style()->polish(dirtyStatusLabel);
	dirtyStatusLabel->update();
}

bool MainWindow::on_tabWidget_tabCloseRequested(int index)
{
	if (askForClose(index))
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(index));
		if (scrollArea != nullptr)
		{
			FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
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


void MainWindow::on_actionCut_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == nullptr)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->cut();
}

void MainWindow::on_actionCopy_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == nullptr)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->copy();
}

void MainWindow::on_actionPaste_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == nullptr)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->paste();
}

void MainWindow::on_actionDelete_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == nullptr)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->deleteSelectedLines();
}

void MainWindow::on_actionSelectAll_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == nullptr)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->selectAll();
}

