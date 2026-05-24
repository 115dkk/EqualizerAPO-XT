/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include "Editor/widgets/FilterCardModel.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/cards/FilterCardEditorFactory.h"

using std::list;
using std::max;
using std::min;
using std::move;
using std::replace;
using std::shared_ptr;
using std::string;
using std::vector;
using std::wstring;

FilterTable::FilterTable(MainWindow* mainWindow, QWidget* parent)
	: QWidget(parent), mainWindow(mainWindow)
{
	gridLayout = new QGridLayout(this);

	QIcon icon(QStringLiteral(":/icons/arrow_right.ico"));
	insertArrow = new QLabel(this);
	insertArrow->setPixmap(icon.pixmap(GUIHelper::scale(QSize(24, 15))));
	insertArrow->setVisible(false);

	factories.append(new ExpressionFilterGUIFactory);
	factories.append(new CommentFilterGUIFactory);
	factories.append(new IncludeFilterGUIFactory);
	factories.append(new DeviceFilterGUIFactory);
	factories.append(new ChannelFilterGUIFactory);
	factories.append(new StageFilterGUIFactory);
	factories.append(new PreampFilterGUIFactory);
	factories.append(new BiQuadFilterGUIFactory);
	factories.append(new DelayFilterGUIFactory);
	factories.append(new CopyFilterGUIFactory);
	factories.append(new GraphicEQFilterGUIFactory);
	factories.append(new ConvolutionFilterGUIFactory);
	factories.append(new VSTPluginFilterGUIFactory);
	factories.append(new LoudnessCorrectionFilterGUIFactory);

	QApplication::instance()->installEventFilter(this);
}

FilterTable::~FilterTable()
{
	if (QApplication::instance() != nullptr)
		QApplication::instance()->removeEventFilter(this);

	qDeleteAll(items);
	items.clear();

	for (IFilterGUIFactory* factory : factories)
		delete factory;
	factories.clear();
}

void FilterTable::initialize(QScrollArea* scrollArea, const QList<shared_ptr<AbstractAPOInfo>>& outputDevices, const QList<shared_ptr<AbstractAPOInfo>>& inputDevices)
{
	this->scrollArea = scrollArea;
	this->outputDevices = outputDevices;
	this->inputDevices = inputDevices;

	for (IFilterGUIFactory* factory : factories)
		factory->initialize(this);
}

void FilterTable::updateDeviceAndChannelMask(shared_ptr<AbstractAPOInfo> selectedDevice, int channelMask)
{
	this->selectedDevice = selectedDevice;
	this->selectedChannelMask = channelMask;

	if (!items.empty())
		updateGuis();
}

void FilterTable::updateGuis()
{
	QElapsedTimer timer;
	timer.start();

	for (Item* item : items)
	{
		if (item->gui != nullptr)
		{
			item->prefs.clear();
			item->gui->storePreferences(item->prefs);
		}
	}

	QLayout* oldLayout = layout();
	if (oldLayout != nullptr)
	{
		while (QLayoutItem* child = oldLayout->takeAt(0))
		{
			if (QWidget* widget = child->widget())
			{
				if (widget != insertArrow)
					delete widget;
			}
			delete child;
		}
		delete oldLayout;
	}

	qDebug("Delete took %d ms", timer.elapsed());
	timer.start();

	gridLayout = new QGridLayout(this);
	gridLayout->setContentsMargins(0, 0, 0, 0);
	gridLayout->setSpacing(0);
	gridLayout->setColumnStretch(0, 0);
	gridLayout->setColumnStretch(1, 1);

	for (IFilterGUIFactory* factory : factories)
		factory->startOfFile(configPath);

	QVector<int> rowDepths = FilterCardModel::calculateDepths(getLines());
	int row = 0;
	for (Item* item : items)
	{
		QString line = item->text;
		IFilterGUI* gui = nullptr;
		bool usingCardEditor = false;
		int pos = line.indexOf(':');
		if (pos != -1)
		{
			QString key = line.mid(0, pos);
			QString value = line.mid(pos + 1);

			// allow to use indentation
			key = key.trimmed();
			QString factoryKey = key;
			QString factoryValue = value;

			for (IFilterGUIFactory* factory : factories)
			{
				gui = factory->createFilterGUI(factoryKey, factoryValue);

				if (gui != nullptr || factoryKey == "")
					break;
			}

			if (gui != nullptr)
			{
				if (renderMode == ModernCards)
				{
					// factoryKey is the command after CommentFilterGUIFactory strips a leading '#',
					// so both "Include: a.txt" and "# Include: a.txt" reach the card factory with
					// the same key. Without this, only the active line would receive the modern
					// card editor and the commented line would fall back to the legacy GUI.
					IFilterGUI* cardGui = FilterCardEditorFactory::create(this, factoryKey, factoryValue);
					if (cardGui != nullptr)
					{
						delete gui;
						gui = cardGui;
						usingCardEditor = true;
					}
				}

				if (!usingCardEditor)
					for (IFilterGUIFactory* factory : factories)
						gui = factory->decorateFilterGUI(gui);
			}
		}

		QWidget* rowWidget = renderMode == ModernCards
			? static_cast<QWidget*>(new FilterCardRow(this, row + 1, item, gui, row < rowDepths.size() ? rowDepths[row] : 0))
			: static_cast<QWidget*>(new FilterTableRow(this, row + 1, item, gui));
		gridLayout->addWidget(rowWidget, row, 0);

		item->gui = gui;

		if (gui != nullptr)
		{
			gui->loadPreferences(item->prefs);

			if (renderMode != ModernCards)
				connect(gui, SIGNAL(updateModel()), this, SLOT(updateModel()));
			connect(gui, SIGNAL(updateChannels()), this, SLOT(updateChannels()));
		}

		row++;
	}

	for (IFilterGUIFactory* factory : factories)
		factory->endOfFile(configPath);

	propagateChannels();

	QToolBar* toolBar = new QToolBar;
	toolBar->setIconSize(GUIHelper::scale(QSize(16, 16)));

	QWidget* spacer = new QWidget;
	spacer->setFixedWidth(GUIHelper::scale(25));
	toolBar->addWidget(spacer);

	QAction* addAction = new QAction(QIcon(":/icons/list-add-green.ico"), tr("Add filter"), toolBar);
	addAction->setCheckable(true);
	connect(addAction, SIGNAL(triggered()), this, SLOT(addActionTriggered()));
	toolBar->addAction(addAction);

	gridLayout->addWidget(toolBar, row++, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);

	QSpacerItem* spacerItem = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
	gridLayout->addItem(spacerItem, row, 0);

	gridLayout->setRowStretch(row, 1);

	disableWheelForWidgets();

	qDebug("Create took %d ms", timer.elapsed());
	update();
}
