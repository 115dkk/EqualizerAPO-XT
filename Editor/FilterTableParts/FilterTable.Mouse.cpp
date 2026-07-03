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
		// Pixel-to-row hit testing stays here; the selection state math lives
		// in FilterListModel. (audit #146 TD032)
		int row = rowForPos(event->pos(), false);
		if (row != -1)
		{
			Item* item = model.items()[row];

			if (event->modifiers() & Qt::ControlModifier)
			{
				if (!model.deselect(item))
					model.select(item);
				model.setSelectionStart(item);
			}
			else if (event->modifiers() & Qt::ShiftModifier)
			{
				model.selectRangeFromAnchor(item);
			}
			else
			{
				// Clicking an already-selected row keeps the multi-selection
				// intact so it can be dragged as a group.
				if (!model.isSelected(item))
					model.selectOnly(item);
				model.setSelectionStart(item);
			}
			model.setFocused(item);
			ensureRowVisible(row);
			updateRowWidgets();

			dragStartPos = event->pos();
		}
		else
		{
			model.clearSelection();
			model.setFocused(nullptr);
			model.setSelectionStart(nullptr);
			updateRowWidgets();
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
			Item* item = model.items()[row];

			if (!(event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier))
			{
				// A plain click that did not turn into a drag collapses the
				// multi-selection down to the clicked row.
				if (model.isSelected(item) && model.selectionStart() == item)
					model.selectOnly(item);
			}
			ensureRowVisible(row);
			updateRowWidgets();
		}
	}
}

void FilterTable::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		if ((event->pos() - dragStartPos).manhattanLength() >= QApplication::startDragDistance())
		{
			// Widget-side pass: persist each selected row's GUI preferences
			// (the drag payload must carry the latest edits) and hit-test the
			// drag start against the row headers. The payload itself is the
			// model's copy payload. (audit #146 TD032)
			int i = 0;
			bool dragPosInside = false;
			for (Item* item : model.items())
			{
				if (model.selected().contains(item))
				{
					if (item->gui != nullptr)
						item->gui->storePreferences(item->prefs);

					if (!dragPosInside)
					{
						QLayoutItem* layoutItem = gridLayout->itemAtPosition(i, 0);
						if (layoutItem == nullptr || layoutItem->widget() == nullptr)
						{
							i++;
							continue;
						}
						QWidget* rowWidget = layoutItem->widget();
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

			if (!model.selected().isEmpty() && dragPosInside)
			{
				FilterListModel::CopyPayload payload = model.copyPayload();
				FilterTableMimeData* mimeData = new FilterTableMimeData;
				mimeData->setText(payload.text);
				mimeData->setPrefsList(payload.prefsList);

				QDrag* drag = new QDrag(this);
				drag->setMimeData(mimeData);
				QSet<Item*> selectedBefore = model.selected();
				internalDrag = true;
				Qt::DropAction action = drag->exec(Qt::MoveAction | Qt::CopyAction);
				internalDrag = false;
				if (action == Qt::MoveAction)
				{
					// An internal move already re-selected the dropped copies
					// (insertLinesFromMimeData); remove the originals.
					model.removeItems(selectedBefore);
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

