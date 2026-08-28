/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Canonical Qt FlowLayout (wrapping layout) example, trimmed for the cards.
	See FlowLayout.h.
*/

#include "FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
	: QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
	setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
	: m_hSpace(hSpacing), m_vSpace(vSpacing)
{
	setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
	// Delete the owned items directly rather than through the virtual takeAt(),
	// which must not be dispatched from a destructor. This is the concrete
	// most-derived layout and owns every item in itemList.
	for (QLayoutItem* item : itemList)
		delete item;
	itemList.clear();
}

void FlowLayout::addItem(QLayoutItem* item)
{
	itemList.append(item);
}

int FlowLayout::horizontalSpacing() const
{
	if (m_hSpace >= 0)
		return m_hSpace;
	return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
	if (m_vSpace >= 0)
		return m_vSpace;
	return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const
{
	return itemList.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
	return itemList.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
	if (index >= 0 && index < itemList.size())
		return itemList.takeAt(index);
	return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
	return Qt::Orientations();
}

bool FlowLayout::hasHeightForWidth() const
{
	return true;
}

int FlowLayout::heightForWidth(int width) const
{
	return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
	QLayout::setGeometry(rect);
	doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
	// Once a real width has been assigned, report the height the flow
	// actually needs at that width. QWidget::sizeHint computes the
	// height-for-width at the hint's own width, and minimumSize() is only
	// one item wide - every further item would then be counted as its own
	// wrap line, inflating the hint to a stacked-column height and padding
	// the hosting card body with dead space.
	const QRect g = geometry();
	if (g.isValid())
		return QSize(g.width(), heightForWidth(g.width()));
	return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	for (const QLayoutItem* item : itemList)
		size = size.expandedTo(item->minimumSize());

	const QMargins margins = contentsMargins();
	size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
	return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
	int left, top, right, bottom;
	getContentsMargins(&left, &top, &right, &bottom);
	const QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
	int x = effectiveRect.x();
	int y = effectiveRect.y();
	int lineHeight = 0;

	for (QLayoutItem* item : itemList)
	{
		const QWidget* widget = item->widget();
		if (widget != nullptr && widget->isHidden())
			continue;

		int spaceX = horizontalSpacing();
		int nextX = x + item->sizeHint().width() + spaceX;
		if (nextX - spaceX > effectiveRect.right() && lineHeight > 0)
		{
			int spaceY = verticalSpacing();
			x = effectiveRect.x();
			y = y + lineHeight + spaceY;
			nextX = x + item->sizeHint().width() + spaceX;
			lineHeight = 0;
		}

		if (!testOnly)
			item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

		x = nextX;
		lineHeight = qMax(lineHeight, item->sizeHint().height());
	}
	return y + lineHeight - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
	QObject* parent = this->parent();
	if (parent == nullptr)
		return -1;
	if (parent->isWidgetType())
	{
		QWidget* pw = static_cast<QWidget*>(parent);
		return pw->style()->pixelMetric(pm, nullptr, pw);
	}
	return static_cast<QLayout*>(parent)->spacing();
}
