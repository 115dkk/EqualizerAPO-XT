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


void FilterTable::mousePressEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton)
	{
		// Pixel-to-row hit testing stays here; the selection state math lives
		// in FilterListModel.
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
			// model's copy payload.
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
				pendingInternalMoveRow = -1;
				Qt::DropAction action = drag->exec(Qt::MoveAction | Qt::CopyAction);
				internalDrag = false;
				const int moveDropRow = pendingInternalMoveRow;
				pendingInternalMoveRow = -1;
				if (action == Qt::MoveAction && moveDropRow >= 0)
				{
					// A same-table move: dropEvent only recorded the drop row,
					// so commit the untouched originals as one document move.
					QList<Item*> itemsInOrder;
					for (Item* item : model.items())
						if (selectedBefore.contains(item))
							itemsInOrder.append(item);
					moveRows(itemsInOrder, moveDropRow);
				}
				else
				{
					// A move that landed elsewhere (another table, another
					// application) removes the originals; a copy keeps them.
					if (action == Qt::MoveAction)
						model.removeItems(selectedBefore);

					if (action != Qt::IgnoreAction)
					{
						emit linesChanged();
						updateGuis();
					}
				}
			}
		}
	}

	QWidget::mouseMoveEvent(event);
}

