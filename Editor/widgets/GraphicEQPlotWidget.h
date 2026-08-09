#pragma once

#include <vector>

#include <QWidget>

#include "Editor/skins/ISkin.h"
#include "filters/graphicEq/GainIterator.h"

// The interactive response plot of the modern GraphicEQ card. Form before
// color: this widget owns the model (the node list), the Hz/dB mapping and
// every input gesture - node drag, double-click insert, Delete, selection,
// arrow nudges, wheel dB zoom, right-drag pan - and delegates every pixel to
// ISkin::paintGraphicEqPlot, so each skin renders the same instrument in its
// own grammar instead of tinting a stock QGraphicsView. The frequency axis is
// pinned to the audible 20 Hz - 20 kHz window (the card never scrolls
// horizontally); the dB axis carries a movable frame (top + span).
class GraphicEQPlotWidget : public QWidget
{
	Q_OBJECT

public:
	explicit GraphicEQPlotWidget(QWidget* parent = nullptr);

	void setNodes(const std::vector<FilterNode>& value);
	const std::vector<FilterNode>& nodes() const;

	// 15/31 lock frequencies to the band layout; -1 is the variable layout.
	void setBandCount(int value);
	int bandCount() const;

	// The node keyboard/readout edits address; -1 while the list is empty.
	int focusedNode() const;
	// Current selection as node indices (empty when nothing is selected).
	const QSet<int>& selectedNodes() const;
	// Model edit used by the readout strip; clamps, keeps the list sorted and
	// emits nodesEdited + focusedNodeChanged (the index can move on reorder).
	void setNodeValues(int index, double hz, double db);

	// dB frame (value at plotRect's top edge + visible span).
	void setFrame(double topDb, double spanDb);
	double frameTopDb() const;
	double frameSpanDb() const;
	// Frames the response: span wide enough for the node gains, centred.
	void frameToResponse();

	QSize sizeHint() const override;

signals:
	// Any model mutation (drag step, add, remove, nudge, readout edit).
	void nodesEdited();
	void focusedNodeChanged(int index);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	QRectF plotRect() const;
	double hzToX(double hz) const;
	double xToHz(double x) const;
	double dbToY(double db) const;
	double yToDb(double y) const;
	int nodeAt(const QPointF& pos) const;
	void setFocusedNode(int index);
	// Moves node values while keeping the vector sorted by frequency;
	// returns the node's index after any reorder.
	int moveNode(int index, double hz, double db);
	void addNodeAt(const QPointF& pos);
	void removeSelected();
	GraphicEQPlotState buildState() const;

	std::vector<FilterNode> nodeList;
	int bands = -1;
	QSet<int> selection;
	int focused = -1;
	int hovered = -1;
	int dragged = -1;
	QPointF panAnchor;
	bool panning = false;
	double topDb = 21.0;
	double spanDb = 42.0;
	QPointF cursorPos;
	bool cursorInside = false;
};
