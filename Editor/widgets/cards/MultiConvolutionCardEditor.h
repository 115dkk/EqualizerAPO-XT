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
class QComboBox;
class QToolButton;
class ReferenceCardView;

// Modern card body for a "MultiConvolution:" line. It is the card-mode
// counterpart of the legacy guis/MultiConvolutionFilterGUI: the same named
// impulse-response reference as ConvolutionCardEditor (skin view, readout,
// import affordance) with a small selector for the single output channel that
// the summed convolution is written to, placed inside the reference grammar
// ("<channel> <file>").
class MultiConvolutionCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	MultiConvolutionCardEditor(FilterTable* filterTable, const QString& outputChannel, const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	// Offer the channels that exist at this row as the output options, so the
	// user picks the target channel instead of typing it.
	void configureChannels(std::vector<std::wstring>& channelNames) override;

private slots:
	void chooseFile();
	void pathCommitted(const QString& text);
	void importToConfig();

private:
	QString resolvedAbsolutePath() const;
	unsigned currentDeviceSampleRate() const;
	void updateFileInfo();

	FilterTable* filterTable = nullptr;
	// The reference as written in the config line (relative stays relative).
	QString path;
	ReferenceCardView* view = nullptr;
	QComboBox* channelCombo = nullptr;
	QToolButton* chooseButton = nullptr;
	QToolButton* editButton = nullptr;
	QToolButton* importButton = nullptr;
};
