/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix skin's Copy renderer: a flat crosspoint grid (input columns ×
	output rows) where each cell encodes the routing coefficient by colour and
	number, in the manner of an audio routing matrix / patch-bay. Best for the
	"is input X routed to output Y, and at what gain" lookup in dense
	multi-channel (7.1 + virtual) configurations. The grid folds: a collapsed
	view lays out only the channels the command involves, the rest of the
	device layout waits behind the +N CH caption cell, and +BUS patches a new
	virtual channel into the board.
*/

#pragma once

#include <QLineEdit>

#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "Editor/SkinTokens.h"

class CrosspointMatrixView : public RoutingView
{
	Q_OBJECT

public:
	CrosspointMatrixView(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens);

	std::vector<Assignment> assignments() const override;
	void galleryShowcase(const QString& state) override;
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	const SkinTokens skinTokens;
	void rebuildMatrix();
	void updateMetrics();
	Assignment& rowAssignment(int outRow);
	int summandIndex(int outRow, const QString& channel) const;
	QRect cellRect(int outRow, int inCol) const;
	bool hitTest(const QPoint& pos, int& outRow, int& inCol) const;
	QRect footerRect() const;
	void commitEditor();
	void openChannelEditor();
	void commitChannelEditor();

	std::vector<Assignment> workingAssignments;
	// Device channel layout; keeps the full routing surface reachable even when
	// the command references few (or no) channels.
	std::vector<std::wstring> deviceChannels;
	// Fixed-source mode (MultiConvolution): input columns come only from
	// portModel.fixedSources and factors are locked to unity; target rows use
	// the same fold as Copy.
	RoutingPortModel portModel;
	CopyRoutingAdapter::Matrix matrix;

	// Channel fold: rowMap translates a grid row back to its seeded
	// assignment; pinned channels stay on the board while their sum is empty.
	bool channelsExpanded = false;
	QStringList pinnedChannels;
	RoutingFold::Fold fold;
	QVector<int> rowMap;

	// Footer caption cells (the demoted-caption grammar of the Device card's
	// reveal toggle) and the per-row remove target for virtual channels.
	QRect revealRect;
	QRect addRect;
	QVector<QRect> removeRects; // indexed like the grid rows; null = none
	int hoveredControl = 0;     // 0 none, 1 reveal, 2 add
	int hoveredRow = -1;

	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editCol = -1;
	QLineEdit* channelEditor = nullptr;

	// Layout metrics (scaled by device pixel ratio automatically via QPainter).
	int rowHeaderWidth = 64;
	int colHeaderHeight = 30;
	int cellW = 52;
	int cellH = 26;
	int footerH = 24;
};

class CrosspointMatrixRoutingRenderer : public IRoutingRenderer
{
public:
	RoutingView* create(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens) override;
	const char* id() const override { return "crosspoint-matrix"; }
};
