/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "Editor/IFilterGUI.h"

class FilterTable;
class FileReferenceController;
class QToolButton;
class ReferenceCardView;

// Modern card body for a "Convolution:" line, the card-mode counterpart of the
// legacy guis/ConvolutionFilterGUI. Presents the impulse response as a named
// reference through the active skin's ReferenceCardView and
// keeps the convolution-specific readout (length, sample rate, device-rate
// mismatch) plus the "import into the config directory" affordance.
class ConvolutionCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	ConvolutionCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void chooseFile();
	void pathCommitted(const QString& text);
	void importToConfig();

private:
	QString resolvedAbsolutePath() const;
	unsigned currentDeviceSampleRate() const;
	void updateFileInfo();

	FilterTable* filterTable = nullptr;
	FileReferenceController* reference = nullptr;
	ReferenceCardView* view = nullptr;
	QToolButton* chooseButton = nullptr;
	QToolButton* importButton = nullptr;
};
