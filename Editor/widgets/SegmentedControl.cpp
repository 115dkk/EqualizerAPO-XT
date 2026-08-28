/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SegmentedControl.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QtMath>

#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"

QRectF SegmentedControlState::segmentRect(double index) const
{
	if (labels.isEmpty())
		return QRectF(rect);
	const double width = rect.width() / static_cast<double>(labels.size());
	return QRectF(rect.left() + index * width, rect.top(), width, rect.height());
}

SegmentedControl::SegmentedControl(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("SegmentedControl"));
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAttribute(Qt::WA_Hover, true);
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens&) {
		update();
	});
}

void SegmentedControl::setLabels(const QStringList& labels)
{
	segmentLabels = labels;
	if (selected >= segmentLabels.size())
		selected = segmentLabels.isEmpty() ? 0 : segmentLabels.size() - 1;
	selectionPosition = selected;
	hovered = -1;
	pressed = -1;
	updateGeometry();
	update();
}

const QStringList& SegmentedControl::labels() const
{
	return segmentLabels;
}

void SegmentedControl::setCurrentIndex(int index)
{
	if (segmentLabels.isEmpty())
		return;
	const int bounded = qBound(0, index, segmentLabels.size() - 1);
	if (bounded == selected)
		return;
	selected = bounded;
	animateSelectionTo(selected);
	update();
	emit currentIndexChanged(selected);
}

int SegmentedControl::currentIndex() const
{
	return selected;
}

QSize SegmentedControl::sizeHint() const
{
	const QFontMetricsF metrics(font());
	double widest = 0.0;
	for (const QString& label : segmentLabels)
		widest = qMax(widest, metrics.horizontalAdvance(label));
	// Padding per cell, and a height that matches the combo boxes it sits
	// beside; a control bar row cannot afford the 40px a standalone button
	// would take.
	const int cellWidth = qCeil(widest) + 18;
	return QSize(cellWidth * qMax(1, segmentLabels.size()), qCeil(metrics.height()) + 10);
}

QSize SegmentedControl::minimumSizeHint() const
{
	const QFontMetricsF metrics(font());
	return QSize(24 * qMax(1, segmentLabels.size()), qCeil(metrics.height()) + 10);
}

void SegmentedControl::setPreviewState(int selectedIndex, int hoveredIndex)
{
	if (selectionAnimation != nullptr)
		selectionAnimation->stop();
	selected = segmentLabels.isEmpty() ? 0 : qBound(0, selectedIndex, segmentLabels.size() - 1);
	selectionPosition = selected;
	hovered = hoveredIndex;
	pressed = -1;
	update();
}

int SegmentedControl::indexAt(const QPointF& position) const
{
	if (segmentLabels.isEmpty() || width() <= 0)
		return -1;
	if (!rect().contains(position.toPoint()))
		return -1;
	const double cell = width() / static_cast<double>(segmentLabels.size());
	const int index = static_cast<int>(position.x() / cell);
	return qBound(0, index, segmentLabels.size() - 1);
}

void SegmentedControl::animateSelectionTo(int index)
{
	if (!isVisible())
	{
		// Nothing to animate on a control nobody is looking at, and animating
		// anyway is not free: a card that sets its initial choice while being
		// built leaves an indicator travelling for 160 ms afterwards, so a
		// screenshot taken in that window catches it part way and differs from
		// run to run. That is how this turned up.
		if (selectionAnimation != nullptr)
			selectionAnimation->stop();
		selectionPosition = index;
		update();
		return;
	}

	if (selectionAnimation == nullptr)
	{
		selectionAnimation = new QVariantAnimation(this);
		selectionAnimation->setEasingCurve(QEasingCurve::OutCubic);
		connect(selectionAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
			selectionPosition = value.toDouble();
			update();
		});
	}
	// Interruptible: retarget from wherever the indicator is, never from the
	// old cell. Clicking through three choices quickly has to look like one
	// travelling indicator, not three restarts.
	selectionAnimation->stop();
	selectionAnimation->setDuration(160);
	selectionAnimation->setStartValue(selectionPosition);
	selectionAnimation->setEndValue(static_cast<double>(index));
	selectionAnimation->start();
}

void SegmentedControl::paintEvent(QPaintEvent*)
{
	QPainter painter(this);

	SegmentedControlState state;
	state.rect = rect();
	state.labels = segmentLabels;
	state.selectedIndex = selected;
	state.selectionPosition = selectionPosition;
	state.hoveredIndex = hovered;
	state.pressedIndex = pressed;
	state.focused = hasFocus();
	state.enabled = isEnabled();

	SkinManager::instance()->paintSegmentedControl(painter, state);
}

void SegmentedControl::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton || !isEnabled())
	{
		QWidget::mousePressEvent(event);
		return;
	}
	pressed = indexAt(event->position());
	update();
}

void SegmentedControl::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mouseReleaseEvent(event);
		return;
	}
	const int released = indexAt(event->position());
	// A press that wanders off its cell before release selects nothing, the
	// way a button that is dragged away from does not fire.
	const bool committed = pressed >= 0 && released == pressed;
	pressed = -1;
	if (committed)
		setCurrentIndex(released);
	update();
}

void SegmentedControl::mouseMoveEvent(QMouseEvent* event)
{
	const int under = indexAt(event->position());
	if (under != hovered)
	{
		hovered = under;
		update();
	}
	QWidget::mouseMoveEvent(event);
}

void SegmentedControl::leaveEvent(QEvent* event)
{
	if (hovered != -1)
	{
		hovered = -1;
		update();
	}
	QWidget::leaveEvent(event);
}

void SegmentedControl::keyPressEvent(QKeyEvent* event)
{
	switch (event->key())
	{
	case Qt::Key_Left:
	case Qt::Key_Up:
		setCurrentIndex(selected - 1);
		return;
	case Qt::Key_Right:
	case Qt::Key_Down:
		setCurrentIndex(selected + 1);
		return;
	case Qt::Key_Home:
		setCurrentIndex(0);
		return;
	case Qt::Key_End:
		setCurrentIndex(segmentLabels.size() - 1);
		return;
	default:
		break;
	}
	QWidget::keyPressEvent(event);
}

void SegmentedControl::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();
}

void SegmentedControl::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}
