/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <QFileDialog>

#include "filters/ConfigFileReference.h"
#include "Editor/widgets/cards/FileReferenceController.h"
#include "services/security/AudioEngineAccess.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "IncludeFilterGUI.h"
#include "ui_IncludeFilterGUI.h"

IncludeFilterGUI::IncludeFilterGUI(FilterTable* filterTable, const QString& path)
	: ui(std::make_unique<Ui::IncludeFilterGUI>()), filterTable(filterTable)
{
	ui->setupUi(this);

	ui->pathLineEdit->setText(path);

	// Frozen legacy row: it stays functional under every skin, so it consults
	// the same chrome hook as the card editors (legacyRow marks it for skins
	// that want to leave the legacy path untouched).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("include");
	rowInfo.command = QStringLiteral("include");
	rowInfo.legacyRow = true;
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	updateFileInfo();
}

IncludeFilterGUI::~IncludeFilterGUI() = default;

// The file the engine includes for the written text (ConfigFileReference):
// the frozen row still has to point at the same file the engine loads.
QFileInfo IncludeFilterGUI::includedFile() const
{
	return QFileInfo(QString::fromStdWString(ConfigFileReference::resolve(
		QDir::toNativeSeparators(filterTable->getConfigPath()).toStdWString(),
		ui->pathLineEdit->text().toStdWString())));
}

void IncludeFilterGUI::store(QString& command, QString& parameters)
{
	command = "Include";
	parameters = ui->pathLineEdit->text();
}

void IncludeFilterGUI::on_selectFileToolButton_clicked()
{
	QFileInfo fileInfo(filterTable->getConfigPath());
	QDir configDir = fileInfo.absoluteDir();
	QString path = ui->pathLineEdit->text();
	if (path.length() > 0)
		fileInfo = includedFile();

	QFileDialog dialog(this, tr("Include file"), fileInfo.absolutePath(), "*.txt");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("E-APO configurations (*.txt)"));
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		ui->pathLineEdit->setText(FileReferenceController::displayPathForBaseDirectory(configDir.absolutePath(), absolutePath));
		updateFileInfo();

		emit updateModel();
	}
}

void IncludeFilterGUI::on_pathLineEdit_editingFinished()
{
	updateFileInfo();

	emit updateModel();
}

void IncludeFilterGUI::on_openFileToolButton_clicked()
{
	if (ui->pathLineEdit->text().length() > 0)
		filterTable->openConfig(includedFile().absoluteFilePath());
}

void IncludeFilterGUI::updateFileInfo()
{
	QString error = "";

	QString path = ui->pathLineEdit->text();
	if (path.length() == 0)
	{
		error = tr("No file selected");
	}
	else
	{
		const QFileInfo fileInfo = includedFile();
		if (!fileInfo.exists())
		{
			error = tr("File not found");
		}
		else
		{
			path = QDir::toNativeSeparators(fileInfo.absoluteFilePath());

			if (!AudioEngineAccess::isReadableByAudioEngine(path.toStdWString()))
				error = tr("The file is not readable for the audio service.\nChange the file permissions or copy the file to the config directory.");
		}
	}

	ui->errorLabel->setVisible(error.length() > 0);
	ui->errorLabel->setText(error);
}
