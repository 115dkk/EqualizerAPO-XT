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

#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
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


void MainWindow::load(QString path)
{
	path = QDir::toNativeSeparators(path);

	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
		FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());

		if (filterTable->getConfigPath() == path)
		{
			ui->tabWidget->setCurrentIndex(i);
			return;
		}
	}

	QElapsedTimer timer;
	timer.start();

	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFile(path.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION)
			{
				QMessageBox::critical(this, tr("Error"), tr("Error while reading configuration file: %0").arg(QString::fromStdWString(StringHelper::getSystemErrorString(error))));
				return;
			}

			// file is being written, so wait
			Sleep(1);
		}
	}

	stringstream inputStream;

	char buf[8192];
	unsigned long bytesRead = -1;
	while (ReadFile(hFile, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead != 0)
	{
		inputStream.write(buf, bytesRead);
	}

	CloseHandle(hFile);

	inputStream.seekg(0);

	QList<QString> lines;
	while (inputStream.good())
	{
		string encodedLine;
		getline(inputStream, encodedLine);
		if (encodedLine.size() > 0 && encodedLine[encodedLine.size() - 1] == '\r')
			encodedLine.resize(encodedLine.size() - 1);

		wstring line = StringHelper::toWString(encodedLine, CP_UTF8);
		if (line.find(L'\uFFFD') != wstring::npos)
			line = StringHelper::toWString(encodedLine, CP_ACP);

		lines.append(QString::fromStdWString(line));
	}

	QFileInfo fileInfo(path);
	FilterTable* filterTable = addTab(fileInfo.fileName(), QDir::toNativeSeparators(fileInfo.absoluteFilePath()), path, lines);

	connect(filterTable, SIGNAL(linesChanged()), this, SLOT(linesChanged()));

	qDebug("Loading took %.1f ms", timer.nsecsElapsed() / 1e6);

	ui->tabWidget->setCurrentIndex(ui->tabWidget->count() - 1);
	updateDirtyStatus();

	recentFiles.removeAll(path);
	recentFiles.prepend(path);
	if (recentFiles.size() > 10)
		recentFiles.removeLast();
	updateRecentFiles();
}

void MainWindow::save(FilterTable* filterTable, QString path)
{
	QElapsedTimer timer;
	timer.start();

	QList<QString> lines = filterTable->getLines();

	bool first = true;
	QByteArray byteArray;
	for (QString line : lines)
	{
		if (first)
			first = false;
		else
			byteArray.append("\r\n");
		byteArray.append(line.toUtf8());
	}

	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFile(path.toStdWString().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION)
			{
				QMessageBox::critical(this, tr("Error"), tr("Error while writing configuration file: %0").arg(QString::fromStdWString(StringHelper::getSystemErrorString(error))));
				return;
			}

			// file is being written, so wait
			Sleep(1);
		}
	}

	unsigned long bytesWritten;
	WriteFile(hFile, byteArray.constData(), byteArray.length(), &bytesWritten, nullptr);
	if (bytesWritten != byteArray.length())
	{
		// should never happen
		QMessageBox::critical(this, tr("Error"), tr("Only %0/%1 bytes have been written!").arg(bytesWritten).arg(byteArray.length()));
	}

	CloseHandle(hFile);

	qDebug("Saving took %.1f ms", timer.nsecsElapsed() / 1e6);

	startAnalysis();
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
