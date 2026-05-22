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


void FilterTable::mousePressEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		int row = rowForPos(event->pos(), false);
		if (row != -1)
		{
			Item* item = items[row];

			if (event->modifiers() & Qt::ControlModifier)
			{
				if (!selected.remove(item))
					selected.insert(item);
				selectionStart = item;
			}
			else if (event->modifiers() & Qt::ShiftModifier)
			{
				int startRow = items.indexOf(selectionStart);
				if (startRow != -1)
				{
					selected.clear();
					for (int i = min(startRow, row); i <= max(startRow, row); i++)
					{
						selected.insert(items[i]);
					}
				}
			}
			else
			{
				if (!selected.contains(item))
				{
					selected.clear();
					selected.insert(item);
				}
				selectionStart = item;
			}
			focused = item;
			ensureRowVisible(row);
			update();

			dragStartPos = event->pos();
		}
		else
		{
			selected.clear();
			update();
		}
	}
}

void FilterTable::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		int row = rowForPos(event->pos(), false);
		if (row != -1)
		{
			Item* item = items[row];

			if (!(event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier))
			{
				if (selected.contains(item) && selectionStart == item)
				{
					selected.clear();
					selected.insert(item);
				}
			}
			ensureRowVisible(row);
			update();
		}
	}
}

void FilterTable::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		if ((event->pos() - dragStartPos).manhattanLength() >= QApplication::startDragDistance())
		{
			QString text;
			QList<QVariantMap> prefsList;
			bool first = true;
			int i = 0;
			bool dragPosInside = false;
			for (Item* item : items)
			{
				if (selected.contains(item))
				{
					if (first)
						first = false;
					else
						text += "\n";
					text += item->text;
					if (item->gui != nullptr)
						item->gui->storePreferences(item->prefs);
					prefsList.append(item->prefs);

					if (!dragPosInside)
					{
						QWidget* rowWidget = gridLayout->itemAtPosition(i, 0)->widget();
						QRect headerRect;
						FilterTableRow* tableRow = qobject_cast<FilterTableRow*>(rowWidget);
						if (tableRow != nullptr)
							headerRect = tableRow->getHeaderRect();
						else
						{
							FilterCardRow* cardRow = qobject_cast<FilterCardRow*>(rowWidget);
							if (cardRow != nullptr)
								headerRect = cardRow->getHeaderRect();
						}
						QRect rect = headerRect.translated(rowWidget->pos());
						if (rect.contains(dragStartPos))
							dragPosInside = true;
					}
				}
				i++;
			}

			if (selected.size() > 0 && dragPosInside)
			{
				FilterTableMimeData* mimeData = new FilterTableMimeData;
				mimeData->setText(text);
				mimeData->setPrefsList(prefsList);

				QDrag* drag = new QDrag(this);
				drag->setMimeData(mimeData);
				QSet<Item*> selectedBefore = selected;
				internalDrag = true;
				Qt::DropAction action = drag->exec(Qt::MoveAction | Qt::CopyAction);
				internalDrag = false;
				if (action == Qt::MoveAction)
				{
					for (Item* item : selectedBefore)
					{
						items.removeOne(item);
						if (focused == item)
							focused = nullptr;
						if (selectionStart == item)
							selectionStart = nullptr;
						selected.remove(item);
						delete item;
					}
				}

				if (action != Qt::IgnoreAction)
				{
					emit linesChanged();
					updateGuis();
				}
			}
		}
	}

	QWidget::mouseMoveEvent(event);
}

