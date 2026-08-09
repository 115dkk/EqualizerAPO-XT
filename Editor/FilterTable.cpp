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

#include <utility>

#include "MainWindow.h"
#include "SkinManager.h"
#include "FilterTableRow.h"
#include "FilterTableMimeData.h"
#include "FilterGUIFactoryRegistry.h"
#include "Editor/helpers/GUIHelper.h"
#include "services/logging/Logging.h"
#include "audio/ChannelLayout.h"
#include "services/registry/WindowsRegistry.h"
#include "FilterTable.h"
#include "Editor/widgets/AddCardRow.h"
#include "Editor/widgets/FilterInsertSeam.h"
#include "Editor/widgets/FilterCardModel.h"
#include "Editor/widgets/FilterCardRow.h"
#include "Editor/widgets/cards/CommentCardEditor.h"
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
	// themselves via REGISTER_FILTER_GUI_FACTORY (see FilterGUIFactoryRegistry).
	// FilterTable owns the returned instances through the collection.
	factories = FilterGUIFactoryRegistry::createFactories();

	// Every mutation - structural (add/delete/paste/drop) or in-row (knob
	// drag, text edit, enable toggle) - already announces itself through
	// linesChanged, so this one self-connection is the whole undo capture.
	// Established before MainWindow's linesChanged connection, so the history
	// is current by the time the instant-mode save reads the document.
	connect(this, &FilterTable::linesChanged, this, &FilterTable::commitToHistory);
}

FilterTable::~FilterTable()
{
	// Only installed while a wheel-scroll gesture is active (see wheelEvent);
	// removing an uninstalled filter is a documented no-op.
	if (QApplication::instance() != nullptr)
		QApplication::instance()->removeEventFilter(this);

	// The items themselves are owned and deleted by the FilterListModel member.

}

void FilterTable::initialize(QScrollArea* scrollArea, const QList<shared_ptr<AbstractAPOInfo>>& outputDevices, const QList<shared_ptr<AbstractAPOInfo>>& inputDevices)
{
	this->scrollArea = scrollArea;
	this->outputDevices = outputDevices;
	this->inputDevices = inputDevices;

	// The resize hook only cares about the scroll area itself, so filter that
	// one object instead of every event in the application. The app-global
	// filter that redirects wheel events away from child widgets is installed
	// per scroll gesture in wheelEvent().
	scrollArea->installEventFilter(this);

	for (const auto& factory : factories)
		factory->initialize(this);
}

void FilterTable::updateDeviceAndChannelMask(shared_ptr<AbstractAPOInfo> selectedDevice, int channelMask)
{
	this->selectedDevice = selectedDevice;
	this->selectedChannelMask = channelMask;

	if (!model.items().isEmpty())
		updateGuis();
}

void FilterTable::clearRows()
{
	// Persist each row's GUI preferences before its widget (and the GUI it owns)
	// is destroyed, then null the pointer so a following updateGuis() does not
	// read a dangling gui in its own preference-save pass.
	for (Item* item : model.items())
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

	// One repaint at the end instead of one per inserted row; on-screen
	// rebuilds of large configs repainted the growing table hundreds of times.
	const bool updatesWereEnabled = updatesEnabled();
	setUpdatesEnabled(false);

	clearRows();

	qDebug("Delete took %d ms", int(timer.elapsed()));
	timer.start();

	gridLayout = new QGridLayout(this);
	gridLayout->setContentsMargins(0, 0, 0, 0);
	gridLayout->setSpacing(0);
	gridLayout->setColumnStretch(0, 0);
	gridLayout->setColumnStretch(1, 1);

	for (const auto& factory : factories)
		factory->startOfFile(configPath);

	// Phase accounting for the "Create took" log below; large configs spend
	// seconds here, so the breakdown tells slow-startup reports apart without
	// a profiler build.
	QElapsedTimer phaseTimer;
	phaseTimer.start();
	qint64 prepareNs = 0, guiNs = 0, rowCtorNs = 0, addNs = 0, channelsNs = 0;

	const QVector<FilterCardBuildPlan> rowPlans = renderMode == ModernCards
		? FilterCardModel::prepareRows(getLines()) : QVector<FilterCardBuildPlan>();
	prepareNs = phaseTimer.nsecsElapsed();
	int row = 0;
	for (Item* item : model.items())
	{
		FilterCardDescriptor descriptor;
		if (renderMode == ModernCards)
			descriptor = row < rowPlans.size()
				? rowPlans[row].descriptor
				: FilterCardModel::describeLine(item->text);
		phaseTimer.start();
		IFilterGUI* gui = createRowGui(item->text,
			renderMode == ModernCards ? &descriptor : nullptr);
		guiNs += phaseTimer.nsecsElapsed();

		// LegacyRows is a frozen fallback that must not be extended; see the
		// RenderMode enum and docs/FilterListUiPolicy.md.
		// Parent the card here instead of letting addWidget() reparent it: moving
		// a finished ~40-widget card under the stylesheet-dressed table forces a
		// style re-resolution of the whole subtree (measured ~3 ms per card).
		// With the parent fixed up front, addWidget() only places the item.
		phaseTimer.start();
		QWidget* rowWidget = renderMode == ModernCards
			? static_cast<QWidget*>(new FilterCardRow(this, row + 1, item, gui, std::move(descriptor), this))
			: static_cast<QWidget*>(new FilterTableRow(this, row + 1, item, gui));
		rowCtorNs += phaseTimer.nsecsElapsed();
		phaseTimer.start();
		gridLayout->addWidget(rowWidget, row, 0);
		addNs += phaseTimer.nsecsElapsed();

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

	for (const auto& factory : factories)
		factory->endOfFile(configPath);

	phaseTimer.start();
	propagateChannels();
	channelsNs = phaseTimer.nsecsElapsed();

	if (renderMode == ModernCards)
	{
		// The skinned "add card" row replaces the legacy green-icon toolbar in
		// the card path (shared insertion contract, docs/skins/README.md). It
		// occupies the same single grid slot, so the incremental splice paths
		// keep their row arithmetic.
		AddCardRow* addCardRow = new AddCardRow(this);
		connect(addCardRow, &AddCardRow::activated, this, [this, addCardRow]() {
			addRowActivated(addCardRow);
		});
		gridLayout->addWidget(addCardRow, row++, 0);
	}
	else
	{
		// Frozen heritage flow: the classic toolbar action and cascading menu.
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
	}

	QSpacerItem* spacerItem = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
	gridLayout->addItem(spacerItem, row, 0);

	gridLayout->setRowStretch(row, 1);

	syncListChrome();

	disableWheelForWidgets();

	setUpdatesEnabled(updatesWereEnabled);

	qDebug("Create took %d ms (prepare %d, editor guis %d, card rows %d, add %d, channels %d, rows %d)",
		int(timer.elapsed()), int(prepareNs / 1000000), int(guiNs / 1000000),
		int(rowCtorNs / 1000000), int(addNs / 1000000), int(channelsNs / 1000000), row);
	update();
}

void FilterTable::addRowActivated(AddCardRow* addCardRow)
{
	// Picker under the row, append, splice one widget into the grid.
	FilterTemplate filterTemplate;
	const QPoint anchor = addCardRow->mapToGlobal(QPoint(8, addCardRow->height() - 4));
	if (chooseFilterTemplate(&filterTemplate, anchor))
	{
		addLine(filterTemplate.getLine());
		insertRowAt(int(model.items().count()) - 1);
	}
}

void FilterTable::insertSeamActivated()
{
	if (insertSeam == nullptr)
		return;

	FilterTemplate filterTemplate;
	const QPoint anchor = insertSeam->mapToGlobal(QPoint(8, insertSeam->height()));
	if (chooseFilterTemplate(&filterTemplate, anchor))
	{
		Item* first = model.items().isEmpty() ? nullptr : model.items().first();
		addLine(filterTemplate.getLine(), first);
		insertRowAt(0);
	}
}

void FilterTable::syncListChrome()
{
	if (renderMode != ModernCards)
	{
		if (insertSeam != nullptr)
			insertSeam->setVisible(false);
		return;
	}

	if (insertSeam == nullptr)
	{
		insertSeam = new FilterInsertSeam(this);
		connect(insertSeam, &FilterInsertSeam::activated, this, &FilterTable::insertSeamActivated);
	}

	// The seam floats over the first card's 4px top margin plus a slice of
	// its frame edge - enough to hit with the mouse, small enough not to
	// shadow the header controls.
	insertSeam->setGeometry(0, 0, width(), 10);
	insertSeam->setVisible(!model.items().isEmpty());
	insertSeam->raise();
}

void FilterTable::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (insertSeam != nullptr)
		insertSeam->setGeometry(0, 0, width(), 10);
}

IFilterGUI* FilterTable::createRowGui(const QString& line, const FilterCardDescriptor* preparedDescriptor)
{
	FilterCardDescriptor derivedDescriptor;
	if (renderMode == ModernCards && preparedDescriptor == nullptr)
	{
		derivedDescriptor = FilterCardModel::describeLine(line);
		preparedDescriptor = &derivedDescriptor;
	}

	// A pure comment has no "command: parameters" shape (with or without an
	// inner colon), so it never reaches the factories below. In card mode it
	// still gets a real editor; the legacy path stays frozen (raw row).
	if (renderMode == ModernCards && preparedDescriptor->type == QStringLiteral("comment"))
		return new CommentCardEditor(line);

	// Copy's maintained card editor is the skin routing view built directly by
	// FilterCardRow. Channel propagation is handled by the row's Qt-free Copy
	// domain logic, so a normal Copy row no longer needs a hidden heritage GUI.
	// Dynamic Copy parameters still fall through: routing editors must not
	// parse and rewrite inline expressions.
	if (renderMode == ModernCards
		&& FilterCardModel::opensRoutingView(*preparedDescriptor)
		&& SkinManager::instance()->routingRenderer() != nullptr)
		return nullptr;

	int pos = line.indexOf(':');
	if (pos == -1)
		return nullptr;

	// allow to use indentation
	QString factoryKey = line.mid(0, pos).trimmed();
	QString factoryValue = line.mid(pos + 1);

	if (renderMode == ModernCards)
	{
		// Card editors take precedence, so ask them before running the legacy
		// chain: constructing a legacy GUI only to replace and delete it would
		// parse VSTPlugin state twice per rebuild. Safe to short-circuit because
		// the card-covered factories keep no per-row state in createFilterGUI
		// (their state is set in initialize()/startOfFile()). Commented lines
		// ("# ...") do not match here and fall through to the chain, where
		// CommentFilterGUIFactory strips the '#'; the post-chain lookup below
		// then gives them the same card editor as their active form.
		IFilterGUI* cardGui = FilterCardEditorFactory::create(this, factoryKey, factoryValue);
		if (cardGui != nullptr)
			return cardGui;
	}

	// Factory results are parentless until the selected row adopts them. Keep
	// the temporary legacy GUI scoped while card-mode replacement is decided,
	// so a card constructor failure cannot strand it.
	std::unique_ptr<IFilterGUI> gui;
	for (const auto& factory : factories)
	{
		gui.reset(factory->createFilterGUI(factoryKey, factoryValue));
		if (gui != nullptr || factoryKey == "")
			break;
	}

	if (gui == nullptr)
		return nullptr;

	if (renderMode == ModernCards)
	{
		// factoryKey is the command after CommentFilterGUIFactory strips a
		// leading '#', so "# Include: a.txt" reaches the card factory with the
		// same key as "Include: a.txt". Without this, only the active line
		// would receive the modern card editor and the commented line would
		// fall back to the legacy GUI.
		IFilterGUI* cardGui = FilterCardEditorFactory::create(this, factoryKey, factoryValue);
		if (cardGui != nullptr)
			return cardGui;
		// In Modern Cards we skip the legacy CommentFilterGUI decorator on
		// purpose: the card row already owns the enable/disable affordance
		// and a second power toolbar inside the body editor only produces
		// rules that disagree with the card header.
		return gui.release();
	}

	// The legacy decorator Interface accepts and may adopt a raw child pointer.
	// Release at that established ownership-transfer seam.
	IFilterGUI* decoratedGui = gui.release();
	for (const auto& factory : factories)
		decoratedGui = factory->decorateFilterGUI(decoratedGui);
	return decoratedGui;
}

void FilterTable::updateSingleRowGui(Item* item)
{
	int rowIndex = model.items().indexOf(item);
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

	// Rebuild the GUI through the same selection policy as updateGuis().
	// Factory startOfFile/endOfFile is skipped intentionally: a single
	// in-place edit (enabled toggle) does not change the surrounding file's
	// structural context, so the include/depth state the factories track
	// stays valid.
	FilterCardDescriptor descriptor;
	QVector<FilterCardRowScope> rowScopes;
	if (renderMode == ModernCards)
	{
		rowScopes = FilterCardModel::calculateScopes(getLines());
		const FilterCardRowScope scope = rowIndex < rowScopes.size() ? rowScopes[rowIndex] : FilterCardRowScope();
		descriptor = FilterCardModel::describeLine(item->text, scope.indent);
		descriptor.logicDepth = scope.logic;
		descriptor.scopeChannels = scope.channels;
	}
	IFilterGUI* gui = createRowGui(item->text,
		renderMode == ModernCards ? &descriptor : nullptr);
	// Same render-mode and parenting policy as updateGuis().
	QWidget* rowWidget = renderMode == ModernCards
		? static_cast<QWidget*>(new FilterCardRow(this, rowIndex + 1, item, gui, std::move(descriptor), this))
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

	// The edited row may be a scope head (a Channel: or If: line whose enable
	// state just flipped): the indent and channel scope of every row below it
	// changes with it, which this in-place path used to leave stale until the
	// next full rebuild. updateRowPosition is cheap (in-place, no widget
	// rebuild) and only re-applies rows whose scope actually changed.
	if (renderMode == ModernCards && !renumberRowsBelow(rowIndex + 1, rowScopes))
	{
		updateGuis();
		return;
	}

	propagateChannels();
	disableWheelForWidgets();
	update();
}

namespace
{
// Moves the layout cell at (fromRow, 0) to (toRow, 0), preserving the cell's
// alignment. QGridLayout has no native row insertion, so the incremental
// paths re-address the reused cells one by one; the widgets themselves are
// not recreated, which is what makes those paths cheap. Returns false when
// the source cell is unexpectedly empty.
bool moveGridCell(QGridLayout* gridLayout, int fromRow, int toRow)
{
	QLayoutItem* cell = gridLayout->itemAtPosition(fromRow, 0);
	if (cell == nullptr)
		return false;

	if (QWidget* widget = cell->widget())
	{
		const Qt::Alignment alignment = cell->alignment();
		// removeWidget deletes the old QWidgetItem, so read the alignment first.
		gridLayout->removeWidget(widget);
		gridLayout->addWidget(widget, toRow, 0, alignment);
	}
	else
	{
		// The stretch spacer is a plain QLayoutItem; removeItem leaves it alive.
		gridLayout->removeItem(cell);
		gridLayout->addItem(cell, toRow, 0);
	}
	return true;
}
}

QVector<QWidget*> FilterTable::rowWidgetsByRow() const
{
	QVector<QWidget*> widgets(int(model.items().count()), nullptr);
	if (gridLayout == nullptr)
		return widgets;
	for (int i = 0; i < gridLayout->count(); i++)
	{
		int row = 0, column = 0, rowSpan = 0, columnSpan = 0;
		gridLayout->getItemPosition(i, &row, &column, &rowSpan, &columnSpan);
		if (column != 0 || row >= widgets.size())
			continue;
		if (QWidget* widget = gridLayout->itemAt(i)->widget())
			widgets[row] = widget;
	}
	return widgets;
}

bool FilterTable::renumberRowsBelow(int firstRow, const QVector<FilterCardRowScope>& rowScopes)
{
	const QVector<QWidget*> rowWidgets = rowWidgetsByRow();
	for (int i = firstRow; i < rowWidgets.size(); i++)
	{
		FilterCardRow* cardRow = qobject_cast<FilterCardRow*>(rowWidgets[i]);
		if (cardRow == nullptr)
			return false;
		cardRow->updateRowPosition(i + 1, i < rowScopes.size() ? rowScopes[i] : FilterCardRowScope());
	}
	return true;
}

void FilterTable::insertRowAt(int index)
{
	// Card-path incremental insert. The model already
	// contains the new item at index; create only that row widget and shift
	// the rows/toolbar/spacer below one grid row down. Any inconsistency falls
	// back to the full rebuild, which is always correct.
	const int itemCount = int(model.items().count());
	if (renderMode != ModernCards || gridLayout == nullptr || index < 0 || index >= itemCount)
	{
		updateGuis();
		return;
	}

	QElapsedTimer timer;
	timer.start();

	// Before the splice the grid still holds the OLD layout: itemCount - 1 row
	// widgets at rows 0..itemCount-2, the toolbar at itemCount-1 and the
	// stretch spacer at itemCount. Shift bottom-up so no two cells collide.
	const int oldSpacerRow = itemCount;
	for (int gridRow = oldSpacerRow; gridRow >= index; gridRow--)
	{
		if (!moveGridCell(gridLayout, gridRow, gridRow + 1))
		{
			updateGuis();
			return;
		}
	}
	gridLayout->setRowStretch(oldSpacerRow, 0);
	gridLayout->setRowStretch(oldSpacerRow + 1, 1);

	// Build the new row through the same selection policy as updateGuis(). The
	// factory startOfFile/endOfFile bracket is skipped like updateSingleRowGui:
	// the stateful factories (Convolution/GraphicEQ/MultiConvolution) only
	// cache the config path there, which an edit inside the loaded file does
	// not change.
	Item* item = model.items().at(index);
	QVector<FilterCardRowScope> rowScopes = FilterCardModel::calculateScopes(getLines());
	FilterCardRowScope scope = index < rowScopes.size() ? rowScopes[index] : FilterCardRowScope();
	FilterCardDescriptor descriptor = FilterCardModel::describeLine(item->text, scope.indent);
	descriptor.logicDepth = scope.logic;
	descriptor.scopeChannels = scope.channels;
	IFilterGUI* gui = createRowGui(item->text, &descriptor);
	QWidget* rowWidget = new FilterCardRow(this, index + 1, item, gui, std::move(descriptor), this);
	gridLayout->addWidget(rowWidget, index, 0);

	item->gui = gui;
	if (gui != nullptr)
	{
		gui->loadPreferences(item->prefs);
		// Card mode never connects updateModel here, matching updateGuis().
		connect(gui, SIGNAL(updateChannels()), this, SLOT(updateChannels()));
	}

	// Rows below the insertion point keep their widgets, but their 1-based
	// number and possibly their channel/If scope changed.
	if (!renumberRowsBelow(index + 1, rowScopes))
	{
		updateGuis();
		return;
	}

	propagateChannels();
	disableWheelForWidgets();
	syncListChrome();
	// The insertion replaced the selection and the row widgets read selection
	// state on paint; repaint them all like a full rebuild would have.
	updateRowWidgets();

	qDebug("Incremental insert took %d ms", int(timer.elapsed()));
	update();
}

void FilterTable::removeRowAt(int index)
{
	// Card-path incremental remove. The model no longer
	// contains the item; delete only its row widget and shift the cells below
	// one grid row up. Any inconsistency falls back to the full rebuild.
	const int itemCount = int(model.items().count());
	if (renderMode != ModernCards || gridLayout == nullptr || index < 0 || index > itemCount)
	{
		updateGuis();
		return;
	}

	QElapsedTimer timer;
	timer.start();

	QLayoutItem* cell = gridLayout->itemAtPosition(index, 0);
	QWidget* oldRow = cell != nullptr ? cell->widget() : nullptr;
	if (oldRow == nullptr)
	{
		updateGuis();
		return;
	}
	gridLayout->removeWidget(oldRow);
	// Delete synchronously (the clearRows() precedent), not via deleteLater():
	// the model already freed the Item this row points at, and a deferred
	// deletion would leave a window where a queued GUI signal (e.g. a VST
	// editor timer emitting updateModel) dereferences the dangling item.
	// deleteSelectedLines is only reached from FilterTable's own key handler
	// and the MainWindow edit actions, never from inside the row widget, so
	// the widget is not on the current event dispatch path.
	delete oldRow;

	// Old grid layout: row widgets at 0..itemCount (one more than the model
	// now has), the toolbar at itemCount+1 and the spacer at itemCount+2.
	const int oldSpacerRow = itemCount + 2;
	for (int gridRow = index + 1; gridRow <= oldSpacerRow; gridRow++)
	{
		if (!moveGridCell(gridLayout, gridRow, gridRow - 1))
		{
			updateGuis();
			return;
		}
	}
	gridLayout->setRowStretch(oldSpacerRow, 0);
	gridLayout->setRowStretch(oldSpacerRow - 1, 1);

	QVector<FilterCardRowScope> rowScopes = FilterCardModel::calculateScopes(getLines());
	if (!renumberRowsBelow(index, rowScopes))
	{
		updateGuis();
		return;
	}

	propagateChannels();
	syncListChrome();
	updateRowWidgets();

	qDebug("Incremental remove took %d ms", int(timer.elapsed()));
	update();
}
