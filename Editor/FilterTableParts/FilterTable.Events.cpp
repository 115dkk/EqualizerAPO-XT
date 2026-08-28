/*
	This file is part of EqualizerAPO-XT, a fork of Equalizer APO.
	Copyright (C) 2014 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 EqualizerAPO-XT contributors
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
#include "Editor/widgets/FilterCardRow.h"

using std::list;
using std::max;
using std::min;
using std::move;
using std::replace;
using std::shared_ptr;
using std::string;
using std::vector;
using std::wstring;


void FilterTable::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up)
	{
		if (model.focused() != nullptr)
		{
			int row = model.items().indexOf(model.focused());
			if (row != -1)
			{
				int newRow = row;
				if (event->key() == Qt::Key_Down && row + 1 < model.items().size())
					newRow = row + 1;
				else if (event->key() == Qt::Key_Up && row - 1 >= 0)
					newRow = row - 1;

				if (newRow != row)
				{
					Item* newFocused = model.items()[newRow];
					model.setFocused(newFocused);
					if (event->modifiers() & Qt::ControlModifier)
					{
					}
					else if (event->modifiers() & Qt::ShiftModifier)
					{
						model.selectRangeFromAnchor(newFocused);
					}
					else
					{
						model.selectOnly(newFocused);
						model.setSelectionStart(newFocused);
					}

					ensureRowVisible(newRow);
					updateRowWidgets();
				}
			}
		}
	}

	if (event->key() == Qt::Key_Space)
	{
		if (model.focused() != nullptr)
		{
			// Plain Space selects; Ctrl+Space toggles.
			if (!(event->modifiers() & Qt::ControlModifier) || !model.deselect(model.focused()))
				model.select(model.focused());
			updateRowWidgets();
		}
	}

	if (event->key() == Qt::Key_F2)
	{
		if (model.focused() != nullptr)
		{
			int rowIndex = model.items().indexOf(model.focused());
			if (rowIndex != -1)
			{
				QLayoutItem* layoutItem = gridLayout->itemAtPosition(rowIndex, 0);
				if (layoutItem == nullptr)
					return;
				FilterTableRow* tableRow = qobject_cast<FilterTableRow*>(layoutItem->widget());
				if (tableRow != nullptr)
					tableRow->editText();
				else
				{
					FilterCardRow* cardRow = qobject_cast<FilterCardRow*>(layoutItem->widget());
					if (cardRow != nullptr)
						cardRow->editText();
				}
			}
		}
	}

	if (event->key() == Qt::Key_Delete)
	{
		deleteSelectedLines();
	}
}

void FilterTable::wheelEvent(QWheelEvent* event)
{
	// While the gesture lasts, wheel events over child widgets must reach the
	// list instead of the widget under the cursor; only an application-wide
	// filter sees events targeted at descendants. Installed here and removed
	// when the gesture ends so idle tables filter nothing.
	if (!appWheelFilterInstalled)
	{
		QApplication::instance()->installEventFilter(this);
		appWheelFilterInstalled = true;
	}
	scrollingNow = true;
	scrollStartPoint = event->globalPosition();

	QWidget::wheelEvent(event);
}

bool FilterTable::eventFilter(QObject* obj, QEvent* event)
{
	QEvent::Type type = event->type();
	if (scrollingNow)
	{
		if (type == QEvent::Wheel)
		{
			QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
			scrollStartPoint = wheelEvent->globalPosition();

			QWidget* widget = qobject_cast<QWidget*>(obj);
			if (widget != nullptr)
			{
				if (isAncestorOf(widget))
				{
					QApplication::sendEvent(parent(), event);
					return true;
				}
			}
		}
		else if (type == QEvent::MouseMove)
		{
			QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

			if ((mouseEvent->globalPos() - scrollStartPoint).manhattanLength() > GUIHelper::scale(30))
			{
				scrollingNow = false;
				if (appWheelFilterInstalled)
				{
					QApplication::instance()->removeEventFilter(this);
					appWheelFilterInstalled = false;
				}
			}
		}
	}

	if (obj == scrollArea && type == QEvent::Resize)
	{
		updateSizeHints();
	}

	return false;
}

void FilterTable::showEvent(QShowEvent*)
{
	if (presetScrollX != -1)
	{
		scrollArea->horizontalScrollBar()->setValue(presetScrollX);
		presetScrollX = -1;
	}

	if (presetScrollY != -1)
	{
		scrollArea->verticalScrollBar()->setValue(presetScrollY);
		presetScrollY = -1;
	}
}

void FilterTable::ensureRowVisible(int row)
{
	QScrollBar* vScrollBar = scrollArea->verticalScrollBar();
	if (vScrollBar != nullptr)
	{
		QRect rect = rowRect(row).toAlignedRect();
		if (rect.top() < vScrollBar->value())
			vScrollBar->setValue(max(0, rect.top()));
		else if (rect.bottom() + 1 > vScrollBar->value() + scrollArea->viewport()->height())
			vScrollBar->setValue(min(vScrollBar->maximum(), rect.bottom() + 1 - scrollArea->viewport()->height()));
	}
}

int FilterTable::rowForPos(QPoint pos, bool insert)
{
	int row = -1;
	for (int i = 0; i < gridLayout->rowCount() - 2; i++)
	{
		QLayoutItem* layoutItem = gridLayout->itemAtPosition(i, 0);
		if (layoutItem == nullptr)
			continue;
		QRect rect = layoutItem->geometry();
		int y;
		if (insert)
			y = rect.center().y();
		else
			y = rect.bottom();

		if (pos.y() <= y)
		{
			row = i;
			break;
		}
	}

	return row;
}

QRectF FilterTable::rowRect(int row)
{
	QLayoutItem* layoutItem = gridLayout->itemAtPosition(row, 0);
	if (layoutItem == nullptr)
		return QRectF();

	QRectF rect = layoutItem->geometry();
	rect = rect.marginsAdded(QMarginsF(-1.5, -1.5, -1.5, -0.5));
	return rect;
}

void FilterTable::disableWheelForWidgets()
{
	QList<QWidget*> widgets = findChildren<QWidget*>();
	for (QWidget* widget : widgets)
	{
		if (qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QDial*>(widget))
		{
			if (!widget->property("eapoWheelFilterInstalled").toBool())
			{
				widget->installEventFilter(new DisableWheelFilter(this, widget));
				widget->setProperty("eapoWheelFilterInstalled", true);
			}
			if (widget->focusPolicy() == Qt::WheelFocus)
				widget->setFocusPolicy(Qt::StrongFocus);
		}
	}
}

void FilterTable::updateRowWidgets()
{
	for (QWidget* rowWidget : rowWidgetsByRow())
	{
		if (FilterCardRow* cardRow = qobject_cast<FilterCardRow*>(rowWidget))
		{
			cardRow->syncVisualState();
			cardRow->update();
		}
		else if (rowWidget != nullptr)
			rowWidget->update();
	}
	update();
}

QString FilterTable::getConfigPath() const
{
	return configPath;
}

void FilterTable::setConfigPath(const QString& value)
{
	configPath = value;
}

void FilterTable::setLoadTraceFacts(const QVector<ConfigLoadTraceEntry>& facts)
{
	loadTraceFacts.clear();
	for (const ConfigLoadTraceEntry& fact : facts)
	{
		if (fact.line > 0)
			loadTraceFacts.insert(fact.line - 1, fact);
	}

	// Dynamic-command presentations cache their CommandRowInfo in the card
	// frame. Synchronize it explicitly so analysis facts and selection use the
	// same state path rather than waiting for an unrelated rebuild.
	if (gridLayout == nullptr)
		return;
	updateRowWidgets();
}

QList<ConfigLoadTraceEntry> FilterTable::loadTraceFactsForRow(int row) const
{
	return loadTraceFacts.values(row);
}

FilterTable::Item* FilterTable::getFocusedItem() const
{
	return model.focused();
}

const QSet<FilterTable::Item*>& FilterTable::getSelectedItems() const
{
	return model.selected();
}

const QList<shared_ptr<AbstractAPOInfo>>& FilterTable::getOutputDevices() const
{
	return outputDevices;
}

const QList<shared_ptr<AbstractAPOInfo>>& FilterTable::getInputDevices() const
{
	return inputDevices;
}

shared_ptr<AbstractAPOInfo> FilterTable::getSelectedDevice() const
{
	return selectedDevice;
}

int FilterTable::getSelectedChannelMask() const
{
	return selectedChannelMask;
}
