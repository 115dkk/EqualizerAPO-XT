/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix skin's Copy renderer: a flat crosspoint grid (input columns ×
	output rows) where each cell encodes the routing coefficient by colour and
	number, in the manner of an audio routing matrix / patch-bay. Best for the
	"is input X routed to output Y, and at what gain" lookup in dense
	multi-channel (7.1 + virtual) configurations.
*/

#pragma once

#include <QLineEdit>

#include "IRoutingRenderer.h"
#include "CopyRoutingAdapter.h"

class CrosspointMatrixView : public RoutingView
{
	Q_OBJECT

public:
	CrosspointMatrixView(const std::vector<Assignment>& assignments, QWidget* parent);

	std::vector<Assignment> assignments() const override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
	void rebuildMatrix();
	int summandIndex(int outRow, const QString& channel) const;
	QRect cellRect(int outRow, int inCol) const;
	bool hitTest(const QPoint& pos, int& outRow, int& inCol) const;
	void commitEditor();

	std::vector<Assignment> workingAssignments;
	CopyRoutingAdapter::Matrix matrix;

	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editCol = -1;

	// Layout metrics (scaled by device pixel ratio automatically via QPainter).
	int rowHeaderWidth = 64;
	int colHeaderHeight = 30;
	int cellW = 52;
	int cellH = 26;
};

class CrosspointMatrixRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, QWidget* parent) override;
	const char* id() const override { return "crosspoint-matrix"; }
};
