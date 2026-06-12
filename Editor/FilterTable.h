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

#pragma once

#include <QLabel>
#include <QListWidget>
#include <QTableWidget>
#include <QMouseEvent>
#include <QGridLayout>
#include <QScrollArea>
#include <QMenu>
#include <QJsonObject>

#include "Editor/helpers/DisableWheelFilter.h"
#include "Editor/widgets/FilterPickerView.h"
#include "DeviceAPOInfo.h"
#include "FilterTemplate.h"
#include "IFilterGUI.h"
#include "IFilterGUIFactory.h"

class MainWindow;

class FilterTable : public QWidget
{
	Q_OBJECT
public:
	// Filter-list render mode. ModernCards (FilterCardRow) is the canonical,
	// actively-maintained filter-list UI; the runtime default below is ModernCards.
	// LegacyRows (FilterTableRow) is FROZEN: it is kept only as a reversible
	// fallback and must NOT be extended. Any new filter-list behavior must be added
	// to the card path (FilterCardRow), not duplicated into the legacy path.
	// See docs/FilterListUiPolicy.md for the rationale.
	enum RenderMode
	{
		ModernCards,
		LegacyRows
	};

	struct Item
	{
		Item()
		{
		}

		Item(const QString& text)
		{
			this->text = text;
		}

		QString text;
		QVariantMap prefs;
		IFilterGUI* gui = nullptr;
	};

	explicit FilterTable(MainWindow* mainWindow, QWidget* parent = 0);
	~FilterTable();
	void initialize(QScrollArea* scrollArea, const QList<std::shared_ptr<AbstractAPOInfo>>& outputDevices, const QList<std::shared_ptr<AbstractAPOInfo>>& inputDevices);
	void updateDeviceAndChannelMask(std::shared_ptr<AbstractAPOInfo> selectedDevice, int selectedChannelMask);
	void updateGuis();
	// Tear down every row widget (and its filter GUI) without rebuilding. Used
	// before a global stylesheet swap on skin change: qApp->setStyleSheet
	// re-polishes every live widget, and a loaded config inflates the tree to
	// thousands of widgets, so clearing first lets the skin apply against just
	// the chrome and the rebuilt rows get polished once on creation.
	void clearRows();
	// Re-create the GUI widget for a single row in place. Used for cheap
	// edits like the enabled toggle, which used to trigger a full updateGuis
	// (delete + rebuild every row in the file).
	void updateSingleRowGui(Item* item);
	void propagateChannels();
	// Channel names for the currently selected device/mask (e.g. L, R, C, ...).
	// Empty when no device is selected. Used both to configure the legacy filter
	// GUIs and to seed the modern card routing editors with the real channel set.
	std::vector<std::wstring> getChannelNames() const;
	QList<QString> getLines();
	void setLines(const QString& configPath, const QList<QString>& lines);
	Item* addLine(const QString& line, Item* before = nullptr);
	void removeItem(Item* item);
	QMenu* createAddPopupMenu();
	// Every insertable template flattened from the factories, in factory order.
	// Feeds the skinnable picker and the offscreen skin gallery.
	QList<FilterPickerEntry> filterPickerEntries() const;
	bool chooseFilterTemplate(FilterTemplate* selectedTemplate, const QPoint& globalPos = QPoint());
	void cut();
	void copy();
	void paste();
	void deleteSelectedLines();
	void selectAll();

	const QList<std::shared_ptr<AbstractAPOInfo>>& getOutputDevices() const;
	const QList<std::shared_ptr<AbstractAPOInfo>>& getInputDevices() const;
	std::shared_ptr<AbstractAPOInfo> getSelectedDevice() const;
	int getSelectedChannelMask() const;

	const QSet<Item*>& getSelectedItems() const;
	Item* getFocusedItem() const;

	QString getConfigPath() const;
	void setConfigPath(const QString& value);

	void openConfig(QString path);

	int getPreferredWidth();
	void updateSizeHints();

	QSize minimumSizeHint() const override;
	void setMinimumHeightHint(int height);

	void savePreferences();
	void setScrollOffsets(int x, int y);

	void updateAnalysis();
	void setRenderMode(RenderMode mode);
	RenderMode getRenderMode() const;

signals:
	void linesChanged();

public slots:
	void updateModel();
	void updateChannels();
	void addActionTriggered();

protected:
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dragMoveEvent(QDragMoveEvent* event) override;
	void dragLeaveEvent(QDragLeaveEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	bool eventFilter(QObject* obj, QEvent* event) override;
	void showEvent(QShowEvent*) override;

private:
	void ensureRowVisible(int row);
	int rowForPos(QPoint pos, bool insert);
	QRectF rowRect(int row);
	void disableWheelForWidgets();
	void updateRowWidgets();

	MainWindow* mainWindow;
	QScrollArea* scrollArea;
	QGridLayout* gridLayout;
	QLabel* insertArrow;
	QPoint dragStartPos;
	bool internalDrag = false;
	QList<Item*> items;
	QSet<Item*> selected;
	Item* focused = nullptr;
	Item* selectionStart = nullptr;
	QList<IFilterGUIFactory*> factories;
	bool scrollingNow = false;
	QPointF scrollStartPoint;
	QList<std::shared_ptr<AbstractAPOInfo>> outputDevices;
	QList<std::shared_ptr<AbstractAPOInfo>> inputDevices;
	std::shared_ptr<AbstractAPOInfo> selectedDevice;
	int selectedChannelMask;
	QString configPath;
	int minimumHeightHint = 0;
	int presetScrollX = -1;
	int presetScrollY = -1;
	RenderMode renderMode = ModernCards;
};

template<typename T> inline uint qHash(const QList<T>& list)
{
	uint hashValue = 0;

	for (T t : list)
		hashValue = 31 * hashValue + qHash(t);

	return hashValue;
}

Q_DECLARE_METATYPE(FilterTable::Item*)
