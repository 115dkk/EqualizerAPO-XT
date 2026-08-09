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

#include "Editor/helpers/ConvolutionPathHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "filters/ConvolutionCommand.h"
#include "services/security/AudioEngineAccess.h"
#include "audio/io/SndfileRAII.h"
#include "ConvolutionFilterGUI.h"
#include "ui_ConvolutionFilterGUI.h"

ConvolutionFilterGUI::ConvolutionFilterGUI(const QString& configPath, unsigned deviceSampleRate, const QString& path)
	: ui(std::make_unique<Ui::ConvolutionFilterGUI>()), deviceSampleRate(deviceSampleRate)
{
	ui->setupUi(this);

	this->configPath = configPath;
	ui->pathLineEdit->setText(path);
	ui->labelError->setWordWrap(true);
	ui->selectFileToolButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	// Replace the dated Tango "drawer" icon baked into the .ui with the modern
	// browse glyph, recoloured to the active text colour so it reads on whichever
	// skin or palette hosts the card.
	ui->selectFileToolButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"),
		palette().color(QPalette::WindowText), 18));

	updateFileInfo();
}

ConvolutionFilterGUI::~ConvolutionFilterGUI() = default;

void ConvolutionFilterGUI::store(QString& command, QString& parameters)
{
	command = "Convolution";

	ConvolutionCommand cmd;
	cmd.path = ui->pathLineEdit->text().toStdWString();
	parameters = QString::fromStdWString(cmd.serialize());
}

void ConvolutionFilterGUI::on_selectFileToolButton_clicked()
{
	QFileInfo fileInfo(configPath);
	QDir configDir = fileInfo.absoluteDir();
	QString path = ui->pathLineEdit->text();
	QString absolutePath = ConvolutionPathHelper::absolutePathForConfig(configPath, path);
	if (!absolutePath.isEmpty())
		fileInfo.setFile(absolutePath);

	QFileDialog dialog(this, tr("Select impulse response file"), fileInfo.absolutePath(), "*.wav;*.flac;*.ogg");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("Impulse response (*.wav *.flac *.ogg)"));
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString selectedPath = dialog.selectedFiles().first();
		ui->pathLineEdit->setText(ConvolutionPathHelper::displayPathForSelection(configPath, selectedPath));
		updateFileInfo();

		emit updateModel();
	}
}

void ConvolutionFilterGUI::on_pathLineEdit_editingFinished()
{
	updateFileInfo();

	emit updateModel();
}

void ConvolutionFilterGUI::updateFileInfo()
{
	bool labelsVisible = true;
	QString error = "";

	QString path = ui->pathLineEdit->text();
	if (path.length() == 0)
	{
		error = tr("No file selected");
		labelsVisible = false;
	}
	else
	{
		QFileInfo fileInfo(ConvolutionPathHelper::absolutePathForConfig(configPath, path));
		if (!fileInfo.exists())
		{
			error = tr("File not found");
			labelsVisible = false;
		}
		else
		{
			path = QDir::toNativeSeparators(fileInfo.absoluteFilePath());

			if (!AudioEngineAccess::isReadableByAudioEngine(path.toStdWString()))
			{
				error = tr("The file is not readable for the audio service.\nChange the file permissions or copy the file to the config directory.");
				labelsVisible = false;
			}
			else
			{
				SF_INFO info = {};
				sndfile::Handle file(sf_wchar_open(path.toStdWString().c_str(), SFM_READ, &info));
				if (!file)
				{
					error = tr("Unsupported file format");
					labelsVisible = false;
				}
				else
				{
					int sampleRate = info.samplerate;
					double length = info.frames * 1000.0 / sampleRate;

					ui->labelLengthValue->setText(tr("%0 ms (%1 samples)").arg(length).arg(info.frames));
					ui->labelSampleRateValue->setText(tr("%0 Hz").arg(sampleRate));
					if (sampleRate != deviceSampleRate)
					{
						error = tr("The file sample rate does not match the device sample rate (%0 Hz)!\nSelect a different file or change the device configuration.").arg(deviceSampleRate);
					}
				}
			}
		}
	}

	ui->labelLength->setVisible(labelsVisible);
	ui->labelLengthValue->setVisible(labelsVisible);
	ui->labelSampleRate->setVisible(labelsVisible);
	ui->labelSampleRateValue->setVisible(labelsVisible);
	ui->labelError->setVisible(error.length() > 0);
	ui->labelError->setText(error);
}
