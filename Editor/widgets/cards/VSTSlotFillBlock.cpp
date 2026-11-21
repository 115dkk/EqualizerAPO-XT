/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "Editor/SkinManager.h"
#include "VSTSlotFillBlock.h"

namespace
{
// The latch child is interaction-only: the block paints its visuals through
// ISkin::paintVstSlotFillBlock. A plain QWidget would render the application
// stylesheet's background over that painting, so this one paints nothing.
class BlockLatch : public QWidget
{
public:
	using QWidget::QWidget;

protected:
	void paintEvent(QPaintEvent*) override
	{
	}
};

bool sameRoles(const QList<VSTSlotFillBlock::CellData>& a, const QList<VSTSlotFillBlock::CellData>& b)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); i++)
	{
		if (a[i].role != b[i].role)
			return false;
	}
	return true;
}
}

VSTSlotFillBlock::Layout VSTSlotFillBlock::computeLayout(const VstSlotFillBlockMetrics& metrics,
	const ColumnWidth& columnWidth, const QList<CellData>& input, const QList<CellData>& output,
	bool latchPresent, bool collapsed)
{
	Layout result;
	const bool hasInput = !input.isEmpty();
	const bool hasOutput = !output.isEmpty();
	if (!hasInput && !hasOutput)
		return result;

	const bool heading = metrics.latchPlacement == VstSlotFillBlockMetrics::LatchPlacement::HeadingLine;
	// A block folds only while both sides exist; a lone side stays open.
	const bool folded = collapsed && latchPresent;
	int left = metrics.padding.left();
	int y = metrics.padding.top();

	if (heading)
	{
		// The heading is the section's title whether or not it folds.
		result.latchRect = QRect(left, y, metrics.latchWidth, metrics.latchHeight);
		result.latchActive = latchPresent;
		if (folded)
		{
			result.size = QSize(result.latchRect.right() + 1 + metrics.padding.right(),
				result.latchRect.bottom() + 1 + metrics.padding.bottom());
			return result;
		}
		y += metrics.latchHeight + metrics.latchGap;
	}
	else if (latchPresent)
	{
		result.latchActive = true;
		if (folded)
		{
			// Folded, the block is the latch alone on the plate: no band, no
			// margin above or below it.
			result.latchRect = QRect(left, 0, metrics.latchWidth, metrics.latchHeight);
			result.size = QSize(result.latchRect.right() + 1, metrics.latchHeight);
			return result;
		}
		result.latchRect = QRect(left, 0, metrics.latchWidth, metrics.latchHeight);
		left += metrics.latchWidth + metrics.latchGap;
	}

	const int captionLeft = left;
	const int cellsLeft = left + (metrics.captionWidth > 0 ? metrics.captionWidth + metrics.captionGap : 0);
	const bool shared = metrics.shareRoleLine && hasInput && hasOutput && sameRoles(input, output);

	// Column widths: one shared set while the rows align slot by slot,
	// otherwise each row measures its own.
	QList<int> inputWidths;
	QList<int> outputWidths;
	for (int i = 0; i < input.size(); i++)
		inputWidths.append(columnWidth(input[i].role, input[i].value, false));
	for (int i = 0; i < output.size(); i++)
		outputWidths.append(columnWidth(output[i].role, output[i].value, true));
	if (shared)
	{
		for (int i = 0; i < inputWidths.size(); i++)
		{
			const int width = qMax(inputWidths[i], outputWidths[i]);
			inputWidths[i] = width;
			outputWidths[i] = width;
		}
	}

	int contentRight = result.latchRect.isNull() ? left : result.latchRect.right() + 1;
	const auto placeRow = [&](const QList<int>& widths, int rowY, bool isOutput, QList<QRect>& cells) {
			if (metrics.captionWidth > 0)
				result.captions.append({ QRect(captionLeft, rowY, metrics.captionWidth, metrics.cellHeight), isOutput });
			int x = cellsLeft;
			for (int width : widths)
			{
				cells.append(QRect(x, rowY, width, metrics.cellHeight));
				contentRight = qMax(contentRight, x + width);
				x += width + metrics.cellGap;
			}
		};
	const auto placeRoles = [&](const QList<CellData>& data, const QList<int>& widths, int lineY, bool isOutput) {
			int x = cellsLeft;
			for (int i = 0; i < data.size(); i++)
			{
				result.roles.append({ QRect(x, lineY, widths[i], metrics.roleLineHeight), data[i].role, isOutput });
				x += widths[i] + metrics.cellGap;
			}
		};

	if (shared)
	{
		placeRow(inputWidths, y, false, result.inputCells);
		y += metrics.cellHeight + metrics.roleGap;
		placeRoles(input, inputWidths, y, false);
		y += metrics.roleLineHeight + metrics.roleGap;
		placeRow(outputWidths, y, true, result.outputCells);
		y += metrics.cellHeight;
	}
	else
	{
		if (hasInput)
		{
			if (metrics.rolesAboveRows)
			{
				placeRoles(input, inputWidths, y, false);
				y += metrics.roleLineHeight + metrics.roleGap;
				placeRow(inputWidths, y, false, result.inputCells);
				y += metrics.cellHeight;
			}
			else
			{
				placeRow(inputWidths, y, false, result.inputCells);
				y += metrics.cellHeight + metrics.roleGap;
				placeRoles(input, inputWidths, y, false);
				y += metrics.roleLineHeight;
			}
		}
		if (hasOutput)
		{
			if (hasInput)
				y += metrics.groupGap;
			// The output roles face the input group in either placement.
			placeRoles(output, outputWidths, y, true);
			y += metrics.roleLineHeight + metrics.roleGap;
			placeRow(outputWidths, y, true, result.outputCells);
			y += metrics.cellHeight;
		}
	}

	if (!heading && !result.latchRect.isNull())
	{
		// The latch stands at the block's left edge, centred on its rows.
		const int top = metrics.padding.top();
		result.latchRect.moveTop(top + (y - top - metrics.latchHeight) / 2);
	}
	result.bandRect = QRect(0, 0, contentRight + metrics.padding.right(), y + metrics.padding.bottom());
	result.size = result.bandRect.size();
	return result;
}

VSTSlotFillBlock::VSTSlotFillBlock(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("VSTSlotFillBlock"));
	latch = new BlockLatch(this);
	latch->setObjectName(QStringLiteral("VSTSlotFillLatch"));
	// Skins' universal QWidget stylesheet rules must not paint a box over
	// the block's own latch rendering.
	latch->setAttribute(Qt::WA_NoSystemBackground, true);
	latch->setStyleSheet(QStringLiteral("background: transparent;"));
	latch->setFocusPolicy(Qt::StrongFocus);
	latch->setCursor(Qt::PointingHandCursor);
	// The rails' string, so the catalogs already carry it.
	latch->setAccessibleName(QCoreApplication::translate("VSTSlotFillRail", "Channel fill"));
	latch->installEventFilter(this);
	latch->setVisible(false);
}

void VSTSlotFillBlock::syncCells(QList<VSTSlotFillCell*>& cells, const QList<CellData>& data, bool output)
{
	if (cells.size() != data.size())
	{
		for (VSTSlotFillCell* cell : cells)
			cell->deleteLater();
		cells.clear();
		for (int i = 0; i < data.size(); i++)
		{
			VSTSlotFillCell* cell = new VSTSlotFillCell(output, i, this);
			cell->setRoleInBlock(true);
			cell->setChannelChoices(choices);
			connect(cell, &VSTSlotFillCell::picked, this,
				[this, output](int slot, const QString& value) { emit slotPicked(output, slot, value); });
			cells.append(cell);
		}
	}
	for (int i = 0; i < data.size(); i++)
	{
		const CellData& d = data[i];
		cells[i]->setContent(d.role, d.value, d.silent, d.defaulted, d.missing);
	}
}

void VSTSlotFillBlock::setRows(const QList<CellData>& input, const QList<CellData>& output)
{
	const bool rebuilt = inputCells.size() != input.size() || outputCells.size() != output.size();
	inputData = input;
	outputData = output;
	syncCells(inputCells, inputData, false);
	syncCells(outputCells, outputData, true);
	if (rebuilt)
	{
		// Keyboard order: the latch, the input cells left to right, then the
		// output cells. Cells created later would otherwise trail the focus
		// chain wherever they happened to be appended.
		QWidget* previous = latch;
		for (QList<VSTSlotFillCell*>* cells : { &inputCells, &outputCells })
		{
			for (VSTSlotFillCell* cell : *cells)
			{
				QWidget::setTabOrder(previous, cell);
				previous = cell;
			}
		}
	}
	relayout();
}

void VSTSlotFillBlock::setChannelChoices(const QStringList& names)
{
	choices = names;
	for (VSTSlotFillCell* cell : inputCells)
		cell->setChannelChoices(names);
	for (VSTSlotFillCell* cell : outputCells)
		cell->setChannelChoices(names);
}

void VSTSlotFillBlock::setLatchPresent(bool present)
{
	if (latchPresent == present)
		return;
	latchPresent = present;
	relayout();
}

void VSTSlotFillBlock::setCollapsed(bool newCollapsed)
{
	if (collapsed == newCollapsed)
		return;
	collapsed = newCollapsed;
	relayout();
}

QSize VSTSlotFillBlock::sizeHint() const
{
	return layout.size;
}

QSize VSTSlotFillBlock::minimumSizeHint() const
{
	// As the rails: a slot list wider than the card is cut at the card's
	// edge rather than widening the card.
	return QSize(qMin(40, layout.size.width()), layout.size.height());
}

void VSTSlotFillBlock::relayout()
{
	SkinManager* skins = SkinManager::instance();
	layout = computeLayout(skins->vstSlotFillBlockMetrics(collapsed),
		[skins](const QString& role, const QString& value, bool output) {
			return skins->vstSlotFillBlockColumnWidth(role, value, output);
		},
		inputData, outputData, latchPresent, collapsed);

	const bool folded = collapsed && latchPresent;
	for (int i = 0; i < inputCells.size(); i++)
	{
		inputCells[i]->setVisible(!folded);
		if (i < layout.inputCells.size())
			inputCells[i]->setGeometry(layout.inputCells[i]);
	}
	for (int i = 0; i < outputCells.size(); i++)
	{
		outputCells[i]->setVisible(!folded);
		if (i < layout.outputCells.size())
			outputCells[i]->setGeometry(layout.outputCells[i]);
	}
	const bool latchShown = layout.latchActive && !layout.latchRect.isNull();
	if (latchShown)
		latch->setGeometry(layout.latchRect);
	latch->setVisible(latchShown);
	updateGeometry();
	update();
}

void VSTSlotFillBlock::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	VstSlotFillBlockState state;
	state.rect = rect();
	state.collapsed = collapsed && latchPresent;
	state.enabled = isEnabled();
	state.bandRect = state.collapsed ? QRect() : layout.bandRect;
	state.latchRect = layout.latchRect;
	state.latchActive = layout.latchActive;
	state.latchHovered = latchHovered;
	state.latchPressed = latchPressed;
	state.latchFocused = latch->hasFocus();
	if (!state.collapsed)
	{
		state.captions = layout.captions;
		state.roles = layout.roles;
	}
	SkinManager::instance()->paintVstSlotFillBlock(painter, state);
}

void VSTSlotFillBlock::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::EnabledChange)
		update();
}

bool VSTSlotFillBlock::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != latch)
		return QWidget::eventFilter(watched, event);
	switch (event->type())
	{
	case QEvent::Enter:
		latchHovered = true;
		update();
		break;
	case QEvent::Leave:
		latchHovered = false;
		update();
		break;
	case QEvent::MouseButtonPress:
		if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
		{
			latchPressed = true;
			update();
			return true;
		}
		break;
	case QEvent::MouseButtonRelease:
		if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
		{
			const bool inside = latchPressed
				&& latch->rect().contains(static_cast<QMouseEvent*>(event)->pos());
			latchPressed = false;
			update();
			if (inside)
				emit latchToggled();
			return true;
		}
		break;
	case QEvent::KeyPress:
	{
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			emit latchToggled();
			return true;
		}
		break;
	}
	case QEvent::FocusIn:
	case QEvent::FocusOut:
		update();
		break;
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}
