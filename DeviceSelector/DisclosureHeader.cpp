/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "DisclosureHeader.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

#include "skins/DeviceSkinPainter.h"

DisclosureHeader::DisclosureHeader(QWidget* parent)
	: QWidget(parent)
{
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setAttribute(Qt::WA_Hover, true);
}

void DisclosureHeader::setTitle(const QString& text)
{
	title = text;
	update();
}

void DisclosureHeader::setChecked(bool value)
{
	if (open == value)
		return;
	open = value;
	update();
	emit toggled(open);
}

bool DisclosureHeader::isChecked() const
{
	return open;
}

QSize DisclosureHeader::sizeHint() const
{
	// The full strip is the hit area; 40px keeps it comfortably clickable.
	return QSize(200, 40);
}

QSize DisclosureHeader::minimumSizeHint() const
{
	return QSize(120, 40);
}

void DisclosureHeader::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	DeviceDisclosureState state;
	state.title = title;
	state.open = open;
	state.hover = hoverValue;
	DeviceSkinPainter::active()->paintDisclosure(painter, rect(), state, DeviceSkinPainter::activeTokens());
}

void DisclosureHeader::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		setChecked(!open);
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void DisclosureHeader::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return)
	{
		setChecked(!open);
		event->accept();
		return;
	}
	QWidget::keyPressEvent(event);
}

void DisclosureHeader::enterEvent(QEnterEvent* event)
{
	animateHover(1.0, 150);
	QWidget::enterEvent(event);
}

void DisclosureHeader::leaveEvent(QEvent* event)
{
	animateHover(0.0, 110);
	QWidget::leaveEvent(event);
}

void DisclosureHeader::animateHover(double target, int duration)
{
	if (hoverAnimation == nullptr)
	{
		hoverAnimation = new QVariantAnimation(this);
		hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
		connect(hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
			hoverValue = value.toDouble();
			update();
		});
	}
	// Interruptible: retarget from the current value.
	hoverAnimation->stop();
	hoverAnimation->setDuration(duration);
	hoverAnimation->setStartValue(hoverValue);
	hoverAnimation->setEndValue(target);
	hoverAnimation->start();
}
