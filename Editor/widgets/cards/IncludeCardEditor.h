/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QFileInfo>

#include "Editor/IFilterGUI.h"

class FilterTable;
class FileReferenceController;
class QToolButton;
class ReferenceCardView;

class IncludeCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void chooseFile();
	void openFile();
	void pathCommitted(const QString& text);
	void importToConfig();

private:
	QFileInfo currentFileInfo() const;
	void updateFileInfo();

	FilterTable* filterTable = nullptr;
	FileReferenceController* reference = nullptr;
	ReferenceCardView* view = nullptr;
	QToolButton* chooseButton = nullptr;
	QToolButton* editButton = nullptr;
	QToolButton* importButton = nullptr;
};
