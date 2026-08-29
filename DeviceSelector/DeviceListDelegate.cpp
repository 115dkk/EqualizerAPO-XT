/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "DeviceListDelegate.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTreeWidget>
#include <QVariantAnimation>

#include "skins/DeviceSkinPainter.h"

DeviceListDelegate::DeviceListDelegate(QTreeWidget* treeWidget)
	: QStyledItemDelegate(treeWidget), tree(treeWidget)
{
	// Hover tracking needs move events even with no button held.
	tree->viewport()->setMouseTracking(true);
	tree->viewport()->installEventFilter(this);
}

namespace
{
DeviceRowState rowStateFor(const QModelIndex& index, const QStyleOptionViewItem& option)
{
	DeviceRowState s;
	s.section = !index.parent().isValid();
	s.connection = index.data(Qt::DisplayRole).toString();
	s.device = index.data(DeviceListDelegate::DeviceNameRole).toString();
	s.state = index.data(DeviceListDelegate::StateTextRole).toString();
	s.checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
	s.installed = index.data(DeviceListDelegate::InstalledRole).toBool();
	s.defaultDevice = index.data(DeviceListDelegate::DefaultDeviceRole).toBool();
	s.unavailable = index.data(DeviceListDelegate::UnavailableRole).toBool();
	s.input = index.data(DeviceListDelegate::InputSideRole).toBool();
	s.selected = option.state.testFlag(QStyle::State_Selected);
	s.index = index.row();
	return s;
}
}

void DeviceListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	DeviceRowState s = rowStateFor(index, option);
	if (s.section)
	{
		const QTreeWidgetItem* item = tree->topLevelItem(index.row());
		s.expanded = item != nullptr && item->isExpanded();
	}
	s.hover = hoverProgress(index);
	s.pressed = pressedIndex.isValid() && QPersistentModelIndex(index) == pressedIndex;
	DeviceSkinPainter::active()->paintRow(*painter, option.rect, s, DeviceSkinPainter::activeTokens());
}

QSize DeviceListDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const QFontMetrics fm(option.font);
	return QSize(220, DeviceSkinPainter::active()->rowHeight(fm, !index.parent().isValid()));
}

bool DeviceListDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
	const bool isSection = !index.parent().isValid();
	if (event->type() == QEvent::MouseButtonPress)
	{
		QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() == Qt::LeftButton)
		{
			if (isSection)
			{
				// Single-click folds/unfolds the section.
				QTreeWidgetItem* item = tree->topLevelItem(index.row());
				if (item != nullptr)
					item->setExpanded(!item->isExpanded());
				return true;
			}
			if (DeviceSkinPainter::active()->toggleRect(option.rect).contains(mouse->pos()))
			{
				pressedIndex = index;
				repaintRow(pressedIndex);
				return true; // the jack owns this press; selection stays put
			}
		}
	}
	else if (event->type() == QEvent::MouseButtonDblClick)
	{
		QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() == Qt::LeftButton)
		{
			if (isSection)
			{
				// The double-click's second press folds again, so rapid clicks
				// keep toggling instead of the view swallowing every other one.
				QTreeWidgetItem* item = tree->topLevelItem(index.row());
				if (item != nullptr)
					item->setExpanded(!item->isExpanded());
				return true;
			}
			// The jack already toggled on the first click's release; eat the
			// double-click so fast jack clicking stays one toggle per pair.
			if (DeviceSkinPainter::active()->toggleRect(option.rect).contains(mouse->pos()))
				return true;
			// Double-click anywhere else on the row toggles the device too.
			// The painted jack is the only single-click control, and nothing
			// tells a first-time user that; double-clicking the device they
			// want is what everyone tries first.
			const Qt::CheckState now = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
			model->setData(index, now == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
			return true;
		}
	}
	else if (event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() == Qt::LeftButton && pressedIndex.isValid())
		{
			const bool inside = QPersistentModelIndex(index) == pressedIndex
				&& DeviceSkinPainter::active()->toggleRect(option.rect).contains(mouse->pos());
			const QPersistentModelIndex released = pressedIndex;
			pressedIndex = QPersistentModelIndex();
			repaintRow(released);
			if (inside && !isSection)
			{
				const Qt::CheckState now = static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
				model->setData(index, now == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
			}
			return true;
		}
		// A release drifting into the toggle after a press elsewhere must not
		// reach the base delegate: its own imagined checkbox rect lives in the
		// same corner and would toggle the device without a press-release pair.
		if (!isSection && DeviceSkinPainter::active()->toggleRect(option.rect).contains(mouse->pos()))
			return true;
	}
	return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void DeviceListDelegate::setForcedHover(const QModelIndex& index)
{
	forcedHover = index;
	tree->viewport()->update();
}

bool DeviceListDelegate::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == tree->viewport())
	{
		if (event->type() == QEvent::MouseMove)
			setHoveredIndex(tree->indexAt(static_cast<QMouseEvent*>(event)->pos()));
		else if (event->type() == QEvent::Leave)
			setHoveredIndex(QModelIndex());
	}
	return QStyledItemDelegate::eventFilter(watched, event);
}

double DeviceListDelegate::hoverProgress(const QModelIndex& index) const
{
	const QPersistentModelIndex key(index);
	if (forcedHover.isValid() && key == forcedHover)
		return 1.0;
	return hoverValues.value(key, 0.0);
}

void DeviceListDelegate::animateHover(const QPersistentModelIndex& index, double target, int duration)
{
	if (!index.isValid())
		return;
	QVariantAnimation*& animation = hoverAnimations[index];
	if (animation == nullptr)
	{
		animation = new QVariantAnimation(this);
		animation->setEasingCurve(QEasingCurve::OutCubic);
		connect(animation, &QVariantAnimation::valueChanged, this, [this, index](const QVariant& value) {
			hoverValues[index] = value.toDouble();
			repaintRow(index);
		});
		connect(animation, &QVariantAnimation::finished, this, [this, index]() {
			// Fully faded-out rows drop their bookkeeping so the maps do not
			// accumulate an entry per row ever hovered.
			if (hoverValues.value(index, 0.0) <= 0.0)
			{
				hoverValues.remove(index);
				QVariantAnimation* done = hoverAnimations.take(index);
				if (done != nullptr)
					done->deleteLater();
			}
		});
	}
	// Interruptible: retarget from the current value, never restart from 0.
	animation->stop();
	animation->setDuration(duration);
	animation->setStartValue(hoverValues.value(index, 0.0));
	animation->setEndValue(target);
	animation->start();
}

void DeviceListDelegate::setHoveredIndex(const QModelIndex& index)
{
	const QPersistentModelIndex key(index.isValid() ? index : QModelIndex());
	if (key == hoveredIndex)
		return;
	if (hoveredIndex.isValid())
		animateHover(hoveredIndex, 0.0, 110);
	hoveredIndex = key;
	if (hoveredIndex.isValid())
		animateHover(hoveredIndex, 1.0, 150);
}

void DeviceListDelegate::repaintRow(const QPersistentModelIndex& index)
{
	if (index.isValid())
		tree->viewport()->update(tree->visualRect(index));
}
