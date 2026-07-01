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

#pragma once

#include "Editor/IFilterGUI.h"

class FilterTable;
class QLabel;
class QLineEdit;
class QToolButton;

// Modern card body for a "MultiConvolution:" line. It is the card-mode
// counterpart of the legacy guis/MultiConvolutionFilterGUI and reuses
// ConvolutionCardEditor's impulse-response path block (file picker, length/rate
// readout, import affordance), adding a small field for the single output
// channel that the summed convolution is written to.
class MultiConvolutionCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	MultiConvolutionCardEditor(FilterTable* filterTable, const QString& outputChannel, const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void chooseFile();
	void pathEdited();
	void importToConfig();

private:
	QString resolvedAbsolutePath() const;
	unsigned currentDeviceSampleRate() const;
	void updateFileInfo();

	FilterTable* filterTable = nullptr;
	QLineEdit* channelEdit = nullptr;
	QLineEdit* pathEdit = nullptr;
	QLabel* infoLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QToolButton* chooseButton = nullptr;
	QToolButton* importButton = nullptr;
};
