/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QToolButton>
#include <QScrollBar>
#include <QToolBar>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QDial>
#include <QJsonDocument>
#include <QSettings>

#include "MainWindow.h"
#include "FilterTableRow.h"
#include "FilterTableMimeData.h"
#include "Editor/helpers/GUIHelper.h"
#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"
#include "services/registry/WindowsRegistry.h"
#include "FilterTable.h"

using std::list;
using std::max;
using std::min;
using std::move;
using std::replace;
using std::shared_ptr;
using std::string;
using std::vector;
using std::wstring;


void FilterTable::cut()
{
	copy();
	deleteSelectedLines();
}

void FilterTable::copy()
{
	// The payload (selected lines in document order + aligned prefs) is pure
	// model state; only the clipboard hand-off stays here.
	if (model.selected().isEmpty())
		return;

	FilterListModel::CopyPayload payload = model.copyPayload();

	FilterTableMimeData* mimeData = new FilterTableMimeData;
	mimeData->setText(payload.text);
	mimeData->setPrefsList(payload.prefsList);
	QClipboard* clipboard = QApplication::clipboard();
	clipboard->setMimeData(mimeData);
}

void FilterTable::paste()
{
	QClipboard* clipboard = QApplication::clipboard();
	const QMimeData* mimeData = clipboard->mimeData();
	if (mimeData->hasText())
	{
		int dropRow = model.firstSelectedIndex();
		if (dropRow == -1)
			dropRow = model.items().size();

		int insertedCount = insertLinesFromMimeData(mimeData, dropRow);

		emit linesChanged();
		// A single pasted line is the common case: splice just that row into
		// the card grid instead of rebuilding every row.
		if (insertedCount == 1 && renderMode == ModernCards)
			insertRowAt(dropRow);
		else
			updateGuis();
	}
}

int FilterTable::insertLinesFromMimeData(const QMimeData* mimeData, int dropRow)
{
	// The QMimeData unpacking stays here; the insertion and the selection
	// replacement are model state.
	QString text = mimeData->text();
	QStringList textLines = text.split("\n");
	QList<QVariantMap> prefsList;
	const FilterTableMimeData* filterTableMimeData = qobject_cast<const FilterTableMimeData*>(mimeData);
	if (filterTableMimeData != nullptr)
		prefsList = filterTableMimeData->getPrefsList();

	return int(model.insertLines(textLines, prefsList, dropRow).count());
}

void FilterTable::deleteSelectedLines()
{
	// A single-row deletion keeps every other row widget alive and splices
	// just one out of the grid. Multi-row deletions (and the frozen legacy
	// path) keep the full rebuild.
	int removedIndex = -1;
	if (renderMode == ModernCards && model.selected().size() == 1)
		removedIndex = int(model.items().indexOf(*model.selected().cbegin()));

	model.deleteSelected();
	emit linesChanged();
	if (removedIndex >= 0)
		removeRowAt(removedIndex);
	else
		updateGuis();
}

void FilterTable::selectAll()
{
	model.selectAll();
	updateRowWidgets();
}


void FilterTable::addActionTriggered()
{
	QAction* addAction = qobject_cast<QAction*>(QObject::sender());
	QToolBar* toolBar = qobject_cast<QToolBar*>(addAction->parentWidget());
	QRect rect = toolBar->actionGeometry(addAction);
	QPoint p = toolBar->mapToGlobal(QPoint(rect.x(), rect.y() + rect.height()));
	addAction->setChecked(false);
	if (renderMode == LegacyRows)
	{
		// Heritage add flow: the classic cascading QMenu, the same one the
		// legacy rows' own + button uses (FilterTableRow::on_actionAdd_triggered).
		QMenu* menu = createAddPopupMenu();
		QAction* chosen = menu->exec(p);
		if (chosen != nullptr)
		{
			FilterTemplate t = chosen->data().value<FilterTemplate>();
			addLine(t.getLine());
			updateGuis();
		}
		menu->deleteLater();
		return;
	}
	FilterTemplate filterTemplate;
	if (chooseFilterTemplate(&filterTemplate, p))
	{
		addLine(filterTemplate.getLine());
		// The toolbar add appends exactly one line; splice just that row into
		// the card grid.
		if (renderMode == ModernCards)
			insertRowAt(int(model.items().count()) - 1);
		else
			updateGuis();
	}
}

void FilterTable::openConfig(QString path)
{
	emit configOpenRequested(path);
}

void FilterTable::savePreferences()
{
	if (!configPath.isEmpty())
	{
		QStringList prefLines;

		for (int i = 0; i < model.items().size(); i++)
		{
			Item* item = model.items()[i];

			if (item->gui != nullptr)
			{
				item->prefs.clear();
				item->gui->storePreferences(item->prefs);
			}

			if (!item->prefs.isEmpty())
			{
				QString command;
				int index = item->text.indexOf(':');
				if (index != -1)
					command = item->text.left(index).trimmed();

				QByteArray byteArray = QJsonDocument::fromVariant(item->prefs).toJson(QJsonDocument::Compact);
				QString prefLine = QString("%0:%1:%2").arg(i + 1).arg(command).arg(QString::fromUtf8(byteArray));
				prefLines.append(prefLine);
			}
		}

		QSettings settings(QString::fromWCharArray(EDITOR_PER_FILE_REGPATH), QSettings::NativeFormat);
		settings.beginGroup(QString(configPath).replace('\\', '|'));
		settings.setValue("rowPrefs", prefLines);
		settings.setValue("scrollX", scrollArea->horizontalScrollBar()->value());
		settings.setValue("scrollY", scrollArea->verticalScrollBar()->value());
		settings.endGroup();
	}
}

void FilterTable::setScrollOffsets(int x, int y)
{
	presetScrollX = x;
	presetScrollY = y;
}

void FilterTable::updateAnalysis()
{
	if (isVisible())
		emit analysisUpdateRequested();
}

