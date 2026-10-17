/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "GridRoutingViewBase.h"

#include <QLineEdit>
#include <QMouseEvent>

#include "Editor/widgets/routing/RoutingGridModel.h"

using std::vector;

GridRoutingViewBase::GridRoutingViewBase(const vector<Assignment>& assignments,
	const vector<std::wstring>& channelNames, const RoutingPortModel& portModel,
	QWidget* parent, const SkinTokens& tokens, const Metrics& metrics,
	const QString& factorEditorName, const QString& channelEditorName)
	: RoutingView(parent), skinTokens(tokens),
	// Seeding every device channel as a row keeps the grid editable even when
	// the command references few (or no) channels; without it an emptied Copy
	// could never be refilled from the GUI. Empty rows are skipped by the
	// serializer, so the config line is unaffected. The fold decides which
	// seeded rows actually reach the board.
	workingAssignments(CopyRoutingAdapter::seedTargets(assignments, channelNames)),
	deviceChannels(channelNames),
	portModel(portModel),
	// Targets the command referenced stay on the board for the whole session,
	// even if their last source is toggled away.
	pinnedChannels(RoutingFold::referencedTargets(assignments)),
	rowHeaderWidth(metrics.rowHeaderWidth),
	colHeaderHeight(metrics.colHeaderHeight),
	cellW(metrics.cellW),
	cellH(metrics.cellH),
	factorEditorName(factorEditorName),
	channelEditor(channelEditorName, true)
{
	setMouseTracking(true);
	// Match the stable painted-routing-view size contract (StepList / BlockChip):
	// horizontal policy Ignored + minimumSizeHint == sizeHint. Deviating from
	// this (Preferred + zero minimum) made Qt's layout geometry pass crash flakily
	// on the maximized initial show.
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	setMinimumSize(0, 0);
}

void GridRoutingViewBase::rebuildMatrix()
{
	fold = RoutingFold::fold(workingAssignments, deviceChannels, pinnedChannels,
		channelsExpanded, portModel.fixedSources);
	rowMap = fold.visibleRows;
	vector<Assignment> visible;
	visible.reserve(rowMap.size());
	for (int row : rowMap)
		visible.push_back(workingAssignments[row]);
	matrix = CopyRoutingAdapter::buildMatrix(visible, fold.inputs);
	updateMetrics();
	syncSizeToHint();
	update();
}

std::vector<Assignment> GridRoutingViewBase::assignments() const
{
	return workingAssignments;
}

void GridRoutingViewBase::galleryShowcase(const QString& state)
{
	if (state == QLatin1String("expanded"))
	{
		channelsExpanded = true;
		rebuildMatrix();
	}
	else if (state == QLatin1String("addChannel"))
	{
		openChannelEditor();
		channelEditor.setText(QStringLiteral("VS"));
	}
}

QSize GridRoutingViewBase::minimumSizeHint() const
{
	// Same as the stable StepList/BlockChip views: report the full content size.
	// The host QScrollArea (its own minimum pinned to 0) isolates this, so the
	// FilterTable grid column is not inflated.
	return sizeHint();
}

QRect GridRoutingViewBase::cellRect(int outRow, int inCol) const
{
	return QRect(rowHeaderWidth + inCol * cellW, colHeaderHeight + outRow * cellH, cellW, cellH);
}

bool GridRoutingViewBase::hitTest(const QPoint& pos, int& outRow, int& inCol) const
{
	if (pos.x() < rowHeaderWidth || pos.y() < colHeaderHeight)
		return false;
	inCol = (pos.x() - rowHeaderWidth) / cellW;
	outRow = (pos.y() - colHeaderHeight) / cellH;
	return outRow >= 0 && outRow < matrix.outputs.size() && inCol >= 0 && inCol < matrix.inputs.size();
}

void GridRoutingViewBase::mousePressEvent(QMouseEvent* event)
{
	for (int r = 0; r < removeRects.size(); ++r)
	{
		if (!removeRects[r].isNull() && removeRects[r].contains(event->pos()))
		{
			const QString channel = matrix.outputs[r];
			const bool changed = RoutingGridModel::removeChannel(workingAssignments, pinnedChannels, channel);
			rebuildMatrix();
			if (changed)
				emit routingChanged();
			return;
		}
	}
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
	{
		channelsExpanded = !channelsExpanded;
		rebuildMatrix();
		return;
	}
	if (!addRect.isNull() && addRect.contains(event->pos()))
	{
		openChannelEditor();
		return;
	}

	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	const QString channel = matrix.inputs[inCol];
	const int idx = RoutingGridModel::summandIndex(workingAssignments[rowMap[outRow]], channel);
	Assignment& a = workingAssignments[rowMap[outRow]];
	if (idx >= 0)
	{
		// Toggle off.
		a.sourceSum.erase(a.sourceSum.begin() + idx);
	}
	else
	{
		// Toggle on at unity gain.
		Assignment::Summand s;
		s.factor = 1.0;
		s.isDecibel = false;
		s.channel = channel.toStdWString();
		a.sourceSum.push_back(s);
	}
	rebuildMatrix();
	emit routingChanged();
}

void GridRoutingViewBase::mouseMoveEvent(QMouseEvent* event)
{
	int control = 0;
	if (!revealRect.isNull() && revealRect.contains(event->pos()))
		control = 1;
	else if (!addRect.isNull() && addRect.contains(event->pos()))
		control = 2;

	int row = -1;
	if (event->pos().x() < rowHeaderWidth && event->pos().y() >= colHeaderHeight)
	{
		const int r = (event->pos().y() - colHeaderHeight) / cellH;
		if (r >= 0 && r < matrix.outputs.size())
			row = r;
	}

	if (control != hoveredControl || row != hoveredRow)
	{
		hoveredControl = control;
		hoveredRow = row;
		bool removable = false;
		if (row >= 0 && !removeRects.value(row).isNull()
			&& removeRects[row].contains(event->pos()))
			removable = true;
		setCursor(control != 0 || removable ? Qt::PointingHandCursor : Qt::ArrowCursor);
		update();
	}
	RoutingView::mouseMoveEvent(event);
}

void GridRoutingViewBase::leaveEvent(QEvent* event)
{
	if (hoveredControl != 0 || hoveredRow >= 0)
	{
		hoveredControl = 0;
		hoveredRow = -1;
		update();
	}
	RoutingView::leaveEvent(event);
}

void GridRoutingViewBase::mouseDoubleClickEvent(QMouseEvent* event)
{
	// Without factors there is nothing to edit; the press that preceded this
	// double-click already toggled the crosspoint.
	if (!portModel.allowFactors)
		return;

	int outRow = -1, inCol = -1;
	if (!hitTest(event->pos(), outRow, inCol))
		return;

	commitEditor();
	editRow = outRow;
	editCol = inCol;

	const CopyRoutingAdapter::Cell cell = matrix.cell(outRow, inCol);
	QString textValue;
	if (cell.present)
		textValue = cell.isDecibel ? QStringLiteral("%1dB").arg(cell.factor) : QString::number(cell.factor);
	else
		textValue = QStringLiteral("1");

	if (editor == nullptr)
	{
		editor = new QLineEdit(this);
		editor->setObjectName(factorEditorName);
		editor->setAlignment(Qt::AlignCenter);
		connect(editor, &QLineEdit::editingFinished, this, &GridRoutingViewBase::commitEditor);
	}
	editor->setGeometry(factorEditorRect(outRow, inCol));
	editor->setText(textValue);
	editor->show();
	editor->setFocus();
	editor->selectAll();
}

void GridRoutingViewBase::commitEditor()
{
	if (editor == nullptr || !editor->isVisible() || editRow < 0)
		return;

	const int outRow = editRow;
	const QString channel = matrix.inputs.value(editCol);
	editRow = editCol = -1;
	QString raw = editor->text().trimmed();
	editor->hide();

	if (!RoutingGridModel::commitFactor(workingAssignments, rowMap.value(outRow, -1),
		channel, raw, portModel.allowFactors))
		return;
	rebuildMatrix();
	emit routingChanged();
}

void GridRoutingViewBase::openChannelEditor()
{
	channelEditor.open(this, &GridRoutingViewBase::commitChannelEditor, channelEditorRect());
}

void GridRoutingViewBase::commitChannelEditor()
{
	QString name;
	if (!channelEditor.take(name))
		return;
	if (RoutingGridModel::addChannel(workingAssignments, pinnedChannels, name))
		rebuildMatrix();
}
