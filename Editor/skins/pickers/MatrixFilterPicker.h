/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix "add filter" picker: a two-axis selection instrument - a
	bus rail of categories on the left, that bus's templates as coordinate
	cells on the right.
	Constitution: docs/skins/matrix.md ("필터 픽커" section).
*/

#pragma once

#include <QVector>

#include "Editor/widgets/FilterPickerView.h"

class MatrixFilterPickerView : public FilterPickerView
{
	Q_OBJECT

public:
	explicit MatrixFilterPickerView(QWidget* parent = nullptr);

	void galleryShowcase(GalleryShowcase kind) override;

	QSize sizeHint() const override;

protected:
	void entriesChanged() override;
	void paintEvent(QPaintEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	// One insertable template, parked at a fixed coordinate of the board.
	struct Cell
	{
		int entryIndex = -1; // original index into the setEntries() list
		QString coordinate;  // stable board coordinate, e.g. "C4"
		QString name;
		QString line;
		bool matches = true; // current scan-query verdict
	};

	// One category: a selectable bus on the left rail.
	struct Bus
	{
		QString label;  // upper-cased category caption
		QString letter; // bus designation, "A".."Z"
		QVector<Cell> cells;
	};

	void rebuildBuses(const QList<FilterPickerEntry>& entries);
	void computeMetrics();
	void applyQuery();
	QVector<int> visibleRows(int busIndex) const;
	bool busHasMatches(int busIndex) const;
	int firstBusWithMatches() const;
	const Cell* cursorCell() const;
	void chooseCursor();
	void moveCursor(int delta);
	void switchBus(int delta);

	QRect headerRect() const;
	QRect bodyRect() const;
	QRect railRect() const;
	QRect entriesRect() const;
	QRect footerRect() const;
	QRect busCellRect(int busIndex) const;
	QRect entryCellRect(int visibleRow) const;

	QFont monoFont(double pointSize, bool bold, double letterSpacing = 0.0) const;

	QVector<Bus> buses;
	QString query;
	int activeBus = -1;
	int cursorRow = -1; // index into visibleRows(activeBus)
	int hoverBus = -1;  // bus cell under the mouse, -1 when none
	int hoverRow = -1;  // visible entry row under the mouse, -1 when none

	// Scaled metrics, computed per entry set.
	int headerH = 0;
	int footerH = 0;
	int cellH = 0;
	int railW = 0;
	int entryW = 0;
	int coordW = 0;
	int bodyRows = 0;
	QSize computedSize;
};
