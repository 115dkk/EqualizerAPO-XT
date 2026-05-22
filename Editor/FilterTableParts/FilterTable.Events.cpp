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
#include "guis/ExpressionFilterGUIFactory.h"
#include "guis/CommentFilterGUIFactory.h"
#include "guis/DeviceFilterGUIFactory.h"
#include "guis/ChannelFilterGUIFactory.h"
#include "guis/StageFilterGUIFactory.h"
#include "guis/PreampFilterGUIFactory.h"
#include "guis/BiQuadFilterGUIFactory.h"
#include "guis/CopyFilterGUIFactory.h"
#include "guis/DelayFilterGUIFactory.h"
#include "guis/IncludeFilterGUIFactory.h"
#include "guis/GraphicEQFilterGUIFactory.h"
#include "guis/ConvolutionFilterGUIFactory.h"
#include "guis/VSTPluginFilterGUIFactory.h"
#include "guis/LoudnessCorrectionFilterGUIFactory.h"
#include "Editor/helpers/GUIHelper.h"
#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
#include "helpers/RegistryHelper.h"
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
		if (focused != nullptr)
		{
			int row = items.indexOf(focused);
			if (row != -1)
			{
				int newRow = row;
				if (event->key() == Qt::Key_Down && row + 1 < items.size())
					newRow = row + 1;
				else if (event->key() == Qt::Key_Up && row - 1 >= 0)
					newRow = row - 1;

				if (newRow != row)
				{
					focused = items[newRow];
					if (event->modifiers() & Qt::ControlModifier)
					{
					}
					else if (event->modifiers() & Qt::ShiftModifier)
					{
						int startRow = items.indexOf(selectionStart);
						if (startRow != -1)
						{
							selected.clear();
							for (int i = min(startRow, newRow); i <= max(startRow, newRow); i++)
							{
								selected.insert(items[i]);
							}
						}
					}
					else
					{
						selected.clear();
						selected.insert(focused);
						selectionStart = focused;
					}

					ensureRowVisible(newRow);
					update();
				}
			}
		}
	}

	if (event->key() == Qt::Key_Space)
	{
		if (!(event->modifiers() & Qt::ControlModifier) || !selected.remove(focused))
			selected.insert(focused);
		update();
	}

	if (event->key() == Qt::Key_F2)
	{
		if (focused != nullptr)
		{
			int rowIndex = items.indexOf(focused);
			if (rowIndex != -1)
			{
				QLayoutItem* layoutItem = gridLayout->itemAtPosition(rowIndex, 0);
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
				scrollingNow = false;
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
		QRect rect = gridLayout->itemAtPosition(i, 0)->geometry();
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
	QRectF rect = gridLayout->itemAtPosition(row, 0)->geometry();
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
			widget->installEventFilter(new DisableWheelFilter(this, widget));
			if (widget->focusPolicy() == Qt::WheelFocus)
				widget->setFocusPolicy(Qt::StrongFocus);
		}
	}
}

QString FilterTable::getConfigPath() const
{
	return configPath;
}

void FilterTable::setConfigPath(const QString& value)
{
	configPath = value;
}

FilterTable::Item* FilterTable::getFocusedItem() const
{
	return focused;
}

const QSet<FilterTable::Item*>& FilterTable::getSelectedItems() const
{
	return selected;
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
