/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The interaction half of the matrix-shaped routing views: Signal Matrix's
	crosspoint grid and Rack's patch-bay button field. Both lay input
	columns across output rows; a press toggles a crosspoint at unity, a
	double-click opens the factor editor over it, the grid folds to the
	channels the command involves behind a reveal control, an add control
	opens the add-channel field, and a hovered virtual row offers a remove
	target. The two skins differ only in what a cell, a header and the
	control strip look like and where the editors sit, so a derived view
	supplies paintEvent, sizeHint and the geometry virtuals below. The
	model under this (RoutingFold, CopyRoutingAdapter, RoutingGridModel) is
	shared with every renderer.
*/

#pragma once

#include <vector>
#include <QRect>
#include <QStringList>
#include <QVector>

#include "Editor/widgets/routing/CopyRoutingAdapter.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/RoutingChannelEditor.h"
#include "Editor/widgets/routing/RoutingFold.h"
#include "Editor/SkinTokens.h"

class QLineEdit;

class GridRoutingViewBase : public RoutingView
{
	Q_OBJECT

public:
	std::vector<Assignment> assignments() const override;
	void galleryShowcase(const QString& state) override;
	QSize minimumSizeHint() const override;

protected:
	// The skin's starting cell metrics. updateMetrics() widens the header
	// column and the cells to the labels it measures.
	struct Metrics
	{
		int rowHeaderWidth = 0;
		int colHeaderHeight = 0;
		int cellW = 0;
		int cellH = 0;
	};

	// The editor names are the skin sheet's selectors for the two fields.
	// The derived constructor must end with rebuildMatrix(): that reaches
	// updateMetrics() and sizeHint(), which are not the skin's yet while
	// this constructor runs.
	GridRoutingViewBase(const std::vector<Assignment>& assignments,
		const std::vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
		QWidget* parent, const SkinTokens& tokens, const Metrics& metrics,
		const QString& factorEditorName, const QString& channelEditorName);

	void paintEvent(QPaintEvent* event) override = 0;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

	// Sets rowHeaderWidth and cellW for the current matrix: the skin's label
	// font decides the width a column really needs.
	virtual void updateMetrics() = 0;
	// Where the factor editor sits over the crosspoint (outRow, inCol).
	virtual QRect factorEditorRect(int outRow, int inCol) const = 0;
	// Where the add-channel field opens; addRect is null before the first
	// paint.
	virtual QRect channelEditorRect() const = 0;

	// Refolds the seeded rows, rebuilds the matrix of the visible ones and
	// resizes to the new hint.
	void rebuildMatrix();
	// A crosspoint cell. The hit-test and the row-header hover invert this
	// arithmetic, so it is the same for every grid skin.
	QRect cellRect(int outRow, int inCol) const;

	const SkinTokens skinTokens;
	std::vector<Assignment> workingAssignments;
	// Device channel layout; keeps the full routing surface reachable even
	// when the command references few (or no) channels.
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

	// The control strip's reveal and add controls and the per-row remove
	// target of a hovered virtual channel, laid out by paintEvent.
	QRect revealRect;
	QRect addRect;
	QVector<QRect> removeRects; // indexed like the grid rows; null = none
	int hoveredControl = 0;     // 0 none, 1 reveal, 2 add
	int hoveredRow = -1;

	int rowHeaderWidth = 0;
	int colHeaderHeight = 0;
	int cellW = 0;
	int cellH = 0;

private:
	bool hitTest(const QPoint& pos, int& outRow, int& inCol) const;
	void commitEditor();
	void openChannelEditor();
	void commitChannelEditor();

	const QString factorEditorName;
	QLineEdit* editor = nullptr;
	int editRow = -1;
	int editCol = -1;
	RoutingChannelEditor channelEditor;
};
