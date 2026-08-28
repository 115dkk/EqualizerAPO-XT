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


void FilterTable::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasText())
	{
		if (event->keyboardModifiers() & Qt::ControlModifier)
			event->setDropAction(Qt::CopyAction);
		else
			event->setDropAction(Qt::MoveAction);
		event->accept();
	}

	QWidget::dragEnterEvent(event);
}

void FilterTable::dragMoveEvent(QDragMoveEvent* event)
{
	if (event->mimeData()->hasText())
	{
		if (event->keyboardModifiers() & Qt::ControlModifier)
			event->setDropAction(Qt::CopyAction);
		else
			event->setDropAction(Qt::MoveAction);

		int dropRow = rowForPos(event->pos(), true);

		int arrowRow = dropRow == -1 ? gridLayout->rowCount() - 2 : dropRow;
		QLayoutItem* layoutItem = gridLayout->itemAtPosition(arrowRow, 0);
		if (layoutItem == nullptr)
		{
			insertArrow->hide();
			event->ignore();
			return;
		}

		QRect rect = layoutItem->geometry();
		insertArrow->move(0, rect.top() - insertArrow->height() / 2 - gridLayout->verticalSpacing() / 2);

		insertArrow->raise();
		insertArrow->show();
	}

	QWidget::dragMoveEvent(event);
}

void FilterTable::dragLeaveEvent(QDragLeaveEvent* event)
{
	insertArrow->hide();
}

void FilterTable::dropEvent(QDropEvent* event)
{
	const QMimeData* mimeData = event->mimeData();
	if (mimeData->hasText())
	{
		if (event->keyboardModifiers() & Qt::ControlModifier)
			event->setDropAction(Qt::CopyAction);
		else
			event->setDropAction(Qt::MoveAction);

		int dropRow = rowForPos(event->pos(), true);
		if (dropRow == -1)
			dropRow = model.items().size();

		if (internalDrag && event->dropAction() == Qt::MoveAction)
		{
			// A same-table move mutates nothing here: mouseMoveEvent commits
			// the whole move through moveRows() once drag->exec returns, so
			// the document changes exactly once.
			pendingInternalMoveRow = dropRow;
			event->accept();
		}
		else
		{
			int insertedCount = insertLinesFromMimeData(mimeData, dropRow);
			event->accept();

			// A same-table copy (Ctrl held at the drop) keeps its rebuild in
			// mouseMoveEvent after drag->exec returns.
			if (!internalDrag)
			{
				emit linesChanged();
				// A single dropped line splices into the card grid.
				if (insertedCount == 1 && renderMode == ModernCards)
					insertRowAt(dropRow);
				else
					updateGuis();
			}
		}
	}

	insertArrow->hide();

	QWidget::dropEvent(event);
}

