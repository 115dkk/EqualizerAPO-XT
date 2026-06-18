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
#include "FilterGUIFactoryRegistry.h"
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

	// The roster and its matching order live in the factory translation units
	// themselves via REGISTER_FILTER_GUI_FACTORY (see FilterGUIFactoryRegistry),
	// so adding a filter GUI no longer means editing this list. FilterTable owns
	// the returned instances and deletes them in its destructor.
	factories = FilterGUIFactoryRegistry::createFactories();

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

void FilterTable::clearRows()
{
	// Persist each row's GUI preferences before its widget (and the GUI it owns)
	// is destroyed, then null the pointer so a following updateGuis() does not
	// read a dangling gui in its own preference-save pass.
	for (Item* item : items)
	{
		if (item->gui != nullptr)
		{
			item->prefs.clear();
			item->gui->storePreferences(item->prefs);
			item->gui = nullptr;
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
	gridLayout = nullptr;
}

void FilterTable::updateGuis()
{
	QElapsedTimer timer;
	timer.start();

	clearRows();

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
				bool usingCardEditor = false;
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

				// In Modern Cards we skip the legacy CommentFilterGUI decorator on
				// purpose: the card row already owns the enable/disable affordance
				// and a second power toolbar inside the body editor only produces
				// rules that disagree with the card header.
				if (!usingCardEditor && renderMode != ModernCards)
					for (IFilterGUIFactory* factory : factories)
						gui = factory->decorateFilterGUI(gui);
			}
		}

		// ModernCards (FilterCardRow) is the canonical filter-list UI; LegacyRows
		// (FilterTableRow) is a frozen fallback that must not be extended. New
		// list behavior goes only into the card path. See docs/FilterListUiPolicy.md.
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

void FilterTable::updateSingleRowGui(Item* item)
{
	int rowIndex = items.indexOf(item);
	if (rowIndex < 0 || gridLayout == nullptr)
		return;

	// Save GUI preferences before tearing down.
	if (item->gui != nullptr)
	{
		item->prefs.clear();
		item->gui->storePreferences(item->prefs);
	}

	QLayoutItem* slot = gridLayout->itemAtPosition(rowIndex, 0);
	if (slot == nullptr || slot->widget() == nullptr)
	{
		// No widget in that slot; fall back to a full refresh so we don't
		// silently leave the row stale.
		updateGuis();
		return;
	}

	QWidget* oldRow = slot->widget();
	gridLayout->removeWidget(oldRow);
	oldRow->deleteLater();

	// Reparse the line and rebuild the GUI exactly the way updateGuis() does,
	// using the same factory chain. Factory startOfFile/endOfFile is skipped
	// intentionally: a single in-place edit (enabled toggle) does not change
	// the surrounding file's structural context, so the include/depth state
	// the factories track stays valid.
	QString line = item->text;
	IFilterGUI* gui = nullptr;
	int colonPos = line.indexOf(':');
	if (colonPos != -1)
	{
		QString key = line.mid(0, colonPos).trimmed();
		QString value = line.mid(colonPos + 1);

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
			bool usingCardEditor = false;
			if (renderMode == ModernCards)
			{
				IFilterGUI* cardGui = FilterCardEditorFactory::create(this, factoryKey, factoryValue);
				if (cardGui != nullptr)
				{
					delete gui;
					gui = cardGui;
					usingCardEditor = true;
				}
			}
			// Same decorator gating as updateGuis(): Modern Cards owns power UI
			// in the card header, no additional CommentFilterGUI toolbar inside.
			if (!usingCardEditor && renderMode != ModernCards)
				for (IFilterGUIFactory* factory : factories)
					gui = factory->decorateFilterGUI(gui);
		}
	}

	QVector<int> rowDepths = FilterCardModel::calculateDepths(getLines());
	int depth = rowIndex < rowDepths.size() ? rowDepths[rowIndex] : 0;
	// Same render-mode policy as updateGuis(): the card path is canonical and the
	// legacy row path is a frozen fallback. See docs/FilterListUiPolicy.md.
	QWidget* rowWidget = renderMode == ModernCards
		? static_cast<QWidget*>(new FilterCardRow(this, rowIndex + 1, item, gui, depth))
		: static_cast<QWidget*>(new FilterTableRow(this, rowIndex + 1, item, gui));
	gridLayout->addWidget(rowWidget, rowIndex, 0);

	item->gui = gui;
	if (gui != nullptr)
	{
		gui->loadPreferences(item->prefs);
		if (renderMode != ModernCards)
			connect(gui, SIGNAL(updateModel()), this, SLOT(updateModel()));
		connect(gui, SIGNAL(updateChannels()), this, SLOT(updateChannels()));
	}

	propagateChannels();
	disableWheelForWidgets();
	update();
}
