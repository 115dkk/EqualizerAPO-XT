#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
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

	if (filterTable->getConfigPath().length() == 0)
	{
		ui->actionSaveAs->trigger();
	}
	else
	{
		save(filterTable, filterTable->getConfigPath());

		QString tabText = ui->tabWidget->tabText(ui->tabWidget->currentIndex());
		if (tabText.endsWith('*'))
			ui->tabWidget->setTabText(ui->tabWidget->currentIndex(), tabText.left(tabText.length() - 1));
		updateDirtyStatus();
	}
}

void MainWindow::on_actionSaveAs_triggered()
{
	FilterTable* filterTable = currentFilterTable();
	if (filterTable == nullptr)
		return;

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
		save(filterTable, savePath);
		filterTable->setConfigPath(QDir::toNativeSeparators(savePath));

		QFileInfo fileInfo(savePath);
		ui->tabWidget->setTabText(ui->tabWidget->currentIndex(), fileInfo.fileName());
		ui->tabWidget->setTabToolTip(ui->tabWidget->currentIndex(), QDir::toNativeSeparators(fileInfo.absoluteFilePath()));
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


bool MainWindow::askForClose(int tabIndex)
{
	bool discarded = false;
	if (ui->tabWidget->tabText(tabIndex).endsWith('*'))
	{
		ui->tabWidget->setCurrentIndex(tabIndex);
		QString configPath = ui->tabWidget->tabToolTip(tabIndex);
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

		switch (result)
		{
		case QMessageBox::Save:
			ui->actionSave->trigger();
			if (ui->tabWidget->tabText(tabIndex).endsWith('*'))
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

