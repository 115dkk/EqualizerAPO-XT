/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

    Modal confirmation step between ConfigDependencyScanner and
    ImportExecutor. Shows what the import will touch, the total size,
    any warnings the scanner produced, and lets the user back out.
*/

#pragma once

#include "ImportManifest.h"

#include <QDialog>
#include <QString>

class QLabel;
class QTreeWidget;

namespace EqAPO::Import
{

class ImportDialog : public QDialog
{
    Q_OBJECT

public:
    ImportDialog(const ImportManifest& manifest, const QString& configDir, QWidget* parent = nullptr);

private:
    void buildUi();

    ImportManifest manifest_;
    QString configDir_;
    QTreeWidget* itemTree_;
    QLabel* summaryLabel_;
    QLabel* warningsLabel_;
};

}
