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


void FilterTable::propagateChannels()
{
	vector<wstring> channelNames;
	if (selectedDevice != nullptr)
		channelNames = ChannelHelper::getChannelNames(selectedDevice->getChannelCount(), selectedChannelMask);

	for (Item* item : items)
	{
		if (item->gui != nullptr)
			item->gui->configureChannels(channelNames);
	}
}

QList<QString> FilterTable::getLines()
{
	QList<QString> result;
	for (Item* item : items)
		result.append(item->text);

	return result;
}

void FilterTable::setLines(const QString& configPath, const QList<QString>& lines)
{
	this->configPath = configPath;

	qDeleteAll(items);
	items.clear();

	for (QString line : lines)
	{
		items.append(new Item(line));
	}

	QSettings settings(QString::fromWCharArray(EDITOR_PER_FILE_REGPATH), QSettings::NativeFormat);
	settings.beginGroup(QString(configPath).replace('\\', '|'));
	QVariant prefsValue = settings.value("rowPrefs");
	QStringList prefLines;
	if (prefsValue.isValid())
		prefLines = prefsValue.toStringList();
	for (QString prefLine : prefLines)
	{
		int index = prefLine.indexOf(':');
		int lineNumber;
		if (index != -1)
			lineNumber = prefLine.left(index).toInt();

		QString prefCommand;
		QString prefString;
		if (lineNumber > 0)
		{
			int index2 = prefLine.indexOf(':', index + 1);
			if (index2 != -1)
			{
				prefCommand = prefLine.mid(index + 1, index2 - index - 1);
				prefString = prefLine.mid(index2 + 1);

				if (lineNumber <= items.size())
				{
					Item* item = items[lineNumber - 1];

					QString command;
					int index = item->text.indexOf(':');
					if (index != -1)
						command = item->text.left(index).trimmed();

					if (command == prefCommand)
						item->prefs = QJsonDocument::fromJson(prefString.toUtf8()).toVariant().toMap();
				}
			}
		}
	}
	setScrollOffsets(settings.value("scrollX", 0).toInt(), settings.value("scrollY", 0).toInt());
	settings.endGroup();

	if (!items.isEmpty())
	{
		focused = items[0];
		selectionStart = items[0];
	}
	else
	{
		focused = nullptr;
		selectionStart = nullptr;
	}

	updateGuis();
}

FilterTable::Item* FilterTable::addLine(const QString& line, FilterTable::Item* before)
{
	Item* newItem = new Item(line);

	if (before != nullptr)
	{
		int index = items.indexOf(before);
		items.insert(index, newItem);
	}
	else
	{
		items.append(newItem);
	}

	emit linesChanged();

	return newItem;
}

void FilterTable::removeItem(FilterTable::Item* item)
{
	items.removeOne(item);
	emit linesChanged();
}

QMenu* FilterTable::createAddPopupMenu()
{
	QHash<QList<QString>, QMenu*> pathMap;
	QMenu* rootMenu = new QMenu;
	pathMap[QStringList()] = rootMenu;

	for (IFilterGUIFactory* f : factories)
	{
		QList<FilterTemplate> templates = f->createFilterTemplates();
		for (FilterTemplate t : templates)
		{
			QMenu* menu = pathMap.value(t.getPath());
			if (menu == nullptr)
			{
				QMenu* parentMenu = rootMenu;
				QStringList currentPath;
				for (QString pathSegment : t.getPath())
				{
					currentPath.append(pathSegment);
					menu = pathMap.value(currentPath);
					if (menu == nullptr)
					{
						menu = new QMenu(pathSegment);
						pathMap.insert(currentPath, menu);
						parentMenu->addMenu(menu);
					}
					parentMenu = menu;
				}
			}

			QAction* action = menu->addAction(t.getName());
			action->setData(QVariant::fromValue(t));
		}
	}

	return rootMenu;
}


void FilterTable::updateModel()
{
	emit linesChanged();
}

void FilterTable::updateChannels()
{
	propagateChannels();
}

int FilterTable::getPreferredWidth()
{
	if (scrollArea == nullptr)
		return width();

	return scrollArea->viewport()->width();
}

void FilterTable::updateSizeHints()
{
	for (int i = 0; i < items.size(); i++)
	{
		FilterTableRow* tableRow = qobject_cast<FilterTableRow*>(gridLayout->itemAtPosition(i, 0)->widget());
		tableRow->updateGeometry();
	}
}

QSize FilterTable::minimumSizeHint() const
{
	QSize size = QWidget::minimumSizeHint();
	if (size.height() < minimumHeightHint)
		size.setHeight(minimumHeightHint);

	return size;
}

void FilterTable::setMinimumHeightHint(int height)
{
	minimumHeightHint = height;
	updateGeometry();
}

