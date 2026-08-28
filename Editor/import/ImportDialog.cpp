/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ImportDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace EqAPO::Import
{

namespace
{

QString formatSize(qint64 bytes)
{
    if (bytes <= 0)
        return QObject::tr("0 B");
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QObject::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QObject::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

}

ImportDialog::ImportDialog(const ImportManifest& manifest, const QString& configDir, QWidget* parent)
    : QDialog(parent)
    , manifest_(manifest)
    , configDir_(configDir)
    , itemTree_(nullptr)
    , summaryLabel_(nullptr)
    , warningsLabel_(nullptr)
{
    setWindowTitle(tr("Import to config directory"));
    setMinimumWidth(620);
    buildUi();
}

void ImportDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setText(tr("%1 file(s), %2 will be copied into %3.")
        .arg(manifest_.items.size())
        .arg(formatSize(manifest_.totalBytes))
        .arg(configDir_));
    layout->addWidget(summaryLabel_);

    itemTree_ = new QTreeWidget(this);
    itemTree_->setColumnCount(3);
    itemTree_->setHeaderLabels({ tr("Kind"), tr("Source"), tr("Size") });
    itemTree_->setRootIsDecorated(false);
    itemTree_->setAlternatingRowColors(true);
    for (const ImportItem& item : manifest_.items)
    {
        auto* row = new QTreeWidgetItem(itemTree_);
        row->setText(0, item.kind);
        row->setText(1, item.sourceAbsolute);
        if (item.exists)
            row->setText(2, formatSize(item.sizeBytes));
        else
            row->setText(2, tr("missing"));
    }
    itemTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    itemTree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    itemTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(itemTree_, 1);

    if (!manifest_.warnings.isEmpty())
    {
        warningsLabel_ = new QLabel(this);
        warningsLabel_->setWordWrap(true);
        warningsLabel_->setStyleSheet(QStringLiteral("color: #c46060;"));
        warningsLabel_->setText(manifest_.warnings.join('\n'));
        layout->addWidget(warningsLabel_);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* okButton = buttons->button(QDialogButtonBox::Ok);
    okButton->setText(tr("Import"));
    okButton->setEnabled(!manifest_.items.isEmpty() && !manifest_.hasErrors);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

}
