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


void FilterTable::cut()
{
	copy();
	deleteSelectedLines();
}

void FilterTable::copy()
{
	QString text;
	QList<QVariantMap> prefsList;
	bool first = true;
	for (Item* item : items)
	{
		if (selected.contains(item))
		{
			if (first)
				first = false;
			else
				text += "\n";
			text += item->text;
			prefsList.append(item->prefs);
		}
	}

	if (selected.size() > 0)
	{
		FilterTableMimeData* mimeData = new FilterTableMimeData;
		mimeData->setText(text);
		mimeData->setPrefsList(prefsList);
		QClipboard* clipboard = QApplication::clipboard();
		clipboard->setMimeData(mimeData);
	}
}

void FilterTable::paste()
{
	QClipboard* clipboard = QApplication::clipboard();
	const QMimeData* mimeData = clipboard->mimeData();
	if (mimeData->hasText())
	{
		int dropRow = items.size();
		for (int i = 0; i < items.size(); i++)
		{
			if (selected.contains(items[i]))
			{
				dropRow = i;
				break;
			}
		}

		QString text = mimeData->text();
		QStringList textLines = text.split("\n");
		QList<QVariantMap> prefsList;
		const FilterTableMimeData* filterTableMimeData = qobject_cast<const FilterTableMimeData*>(mimeData);
		if (filterTableMimeData != nullptr)
			prefsList = filterTableMimeData->getPrefsList();

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

		emit linesChanged();
		updateGuis();
	}
}

void FilterTable::deleteSelectedLines()
{
	QList<Item*> newItems;
	for (Item* item : items)
	{
		if (selected.contains(item))
		{
			if (item == focused)
				focused = nullptr;
			if (item == selectionStart)
				selectionStart = nullptr;
			delete item;
		}
		else
		{
			newItems.append(item);
		}
	}
	selected.clear();
	items = newItems;
	emit linesChanged();
	updateGuis();
}

void FilterTable::selectAll()
{
	selected.clear();
	for (Item* item : items)
		selected.insert(item);
	updateRowWidgets();
}


void FilterTable::addActionTriggered()
{
	QAction* addAction = qobject_cast<QAction*>(QObject::sender());
	QToolBar* toolBar = qobject_cast<QToolBar*>(addAction->parentWidget());
	QRect rect = toolBar->actionGeometry(addAction);
	QPoint p = toolBar->mapToGlobal(QPoint(rect.x(), rect.y() + rect.height()));
	addAction->setChecked(false);
	FilterTemplate filterTemplate;
	if (chooseFilterTemplate(&filterTemplate, p))
	{
		addLine(filterTemplate.getLine());
		updateGuis();
	}
}

void FilterTable::openConfig(QString path)
{
	mainWindow->load(path);
}

void FilterTable::savePreferences()
{
	if (!configPath.isEmpty())
	{
		QStringList prefLines;

		for (int i = 0; i < items.size(); i++)
		{
			Item* item = items[i];

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
		mainWindow->startAnalysis();
}

