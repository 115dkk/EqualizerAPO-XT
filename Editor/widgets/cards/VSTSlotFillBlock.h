/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The joined channel-fill block of the modern VST card
	(VstSlotFillArrangement::JoinedBlockBelowBody): the input row of slot
	cells, the slot roles and the output row in one block below the card
	body, with IN/OUT row captions and the fold latch inside the block.

	One widget owns both rows because the shared role line, the column
	alignment and the closed band all need both sides at once. The cells are
	the split rails' VSTSlotFillCell (menu, keyboard, focus, accessibility),
	told to leave the role to the block. All geometry comes from the active
	skin (ISkin::vstSlotFillBlockMetrics, ISkin::vstSlotFillBlockColumnWidth)
	through computeLayout, and all painting from ISkin::paintVstSlotFillBlock
	and ISkin::paintVstSlotFillCell; the host (VSTCardEditor) owns the
	semantics through VSTSlotFillModel, as it does for the rails.
*/

#pragma once

#include <functional>

#include <QList>
#include <QStringList>
#include <QWidget>

#include "Editor/skins/ISkin.h"
#include "VSTSlotFillRail.h"

class VSTSlotFillBlock : public QWidget
{
	Q_OBJECT

public:
	using CellData = VSTSlotFillRail::CellData;

	// Where everything goes, in widget coordinates. Pure geometry, so the
	// --selftest-vst fill gate checks the same layout the widget uses.
	struct Layout
	{
		QSize size;
		QRect bandRect;
		QRect latchRect;
		bool latchActive = false;
		QList<QRect> inputCells;
		QList<QRect> outputCells;
		QList<VstSlotFillBlockState::Caption> captions;
		QList<VstSlotFillBlockState::Role> roles;
	};
	// The width of one column for a role and a value (the skin's
	// vstSlotFillBlockColumnWidth).
	using ColumnWidth = std::function<int(const QString& role, const QString& value, bool output)>;
	// An empty list means that side has no fill (an Auto side). latchPresent:
	// both sides exist, so the block folds.
	static Layout computeLayout(const VstSlotFillBlockMetrics& metrics, const ColumnWidth& columnWidth,
		const QList<CellData>& input, const QList<CellData>& output, bool latchPresent, bool collapsed);

	explicit VSTSlotFillBlock(QWidget* parent = nullptr);

	void setRows(const QList<CellData>& input, const QList<CellData>& output);
	void setChannelChoices(const QStringList& names);
	void setLatchPresent(bool present);
	void setCollapsed(bool collapsed);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void slotPicked(bool output, int slot, const QString& value);
	void latchToggled();

protected:
	void paintEvent(QPaintEvent* event) override;
	void changeEvent(QEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void syncCells(QList<VSTSlotFillCell*>& cells, const QList<CellData>& data, bool output);
	void relayout();

	QList<CellData> inputData;
	QList<CellData> outputData;
	QList<VSTSlotFillCell*> inputCells;
	QList<VSTSlotFillCell*> outputCells;
	QStringList choices;
	bool latchPresent = false;
	bool collapsed = false;
	Layout layout;
	// Transparent interactive child over the latch area; the block paints
	// it through ISkin::paintVstSlotFillBlock.
	QWidget* latch = nullptr;
	bool latchHovered = false;
	bool latchPressed = false;
};
