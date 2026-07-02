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

		QString text = mimeData->text();
		QStringList textLines = text.split("\n");
		QList<QVariantMap> prefsList;
		const FilterTableMimeData* filterTableMimeData = qobject_cast<const FilterTableMimeData*>(mimeData);
		if (filterTableMimeData != nullptr)
			prefsList = filterTableMimeData->getPrefsList();

		int dropRow = rowForPos(event->pos(), true);
		if (dropRow == -1)
			dropRow = items.size();

		selected.clear();
		focused = nullptr;
		selectionStart = nullptr;
		for (int i = 0; i < textLines.size(); i++)
		{
			QString line = textLines[i];
			Item* item = new Item(line);
			if (i < prefsList.size())
				item->prefs = prefsList[i];
			selected.insert(item);
			items.insert(dropRow++, item);
			if (focused == nullptr)
			{
				focused = item;
				selectionStart = item;
			}
		}
		event->accept();

		if (!internalDrag)
		{
			emit linesChanged();
			updateGuis();
		}
	}

	insertArrow->hide();

	QWidget::dropEvent(event);
}

