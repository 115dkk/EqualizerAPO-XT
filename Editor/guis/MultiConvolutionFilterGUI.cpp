/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include "MultiConvolutionFilterGUI.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

#include "filters/MultiConvolutionCommand.h"

MultiConvolutionFilterGUI::MultiConvolutionFilterGUI(const QString& configPath, const QString& outputChannel, const QString& path)
	: configPath(configPath)
{
	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	layout->addWidget(new QLabel(tr("Output channel:"), this));
	channelEdit = new QLineEdit(outputChannel.trimmed(), this);
	channelEdit->setMaximumWidth(80);
	channelEdit->setToolTip(tr("Output channel the summed convolution is written to"));
	connect(channelEdit, SIGNAL(editingFinished()), this, SIGNAL(updateModel()));
	layout->addWidget(channelEdit);

	layout->addWidget(new QLabel(tr("Impulse response:"), this));
	pathEdit = new QLineEdit(path.trimmed(), this);
	connect(pathEdit, SIGNAL(editingFinished()), this, SIGNAL(updateModel()));
	layout->addWidget(pathEdit, 1);

	QToolButton* selectButton = new QToolButton(this);
	selectButton->setText(QStringLiteral("..."));
	selectButton->setToolTip(tr("Select impulse response file"));
	connect(selectButton, SIGNAL(clicked()), this, SLOT(selectFile()));
	layout->addWidget(selectButton);
}

void MultiConvolutionFilterGUI::store(QString& command, QString& parameters)
{
	command = QStringLiteral("MultiConvolution");

	MultiConvolutionCommand cmd;
	cmd.outputChannel = channelEdit->text().trimmed().toStdWString();
	cmd.path = pathEdit->text().toStdWString();
	parameters = QString::fromStdWString(cmd.serialize());
}

void MultiConvolutionFilterGUI::selectFile()
{
	QString file = QFileDialog::getOpenFileName(this, tr("Select impulse response file"), configPath,
			tr("Impulse response (*.wav *.flac *.ogg)"));
	if (!file.isEmpty())
	{
		pathEdit->setText(file);
		emit updateModel();
	}
}
