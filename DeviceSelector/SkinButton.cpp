/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "SkinButton.h"

#include <QPainter>
#include <QVariantAnimation>

#include "skins/DeviceSkinPainter.h"

SkinButton::SkinButton(const QString& text, bool primaryButton, QWidget* parent)
	: QPushButton(text, parent), primary(primaryButton)
{
	setCursor(Qt::PointingHandCursor);
}

QSize SkinButton::sizeHint() const
{
	return DeviceSkinPainter::active()->buttonSizeHint(fontMetrics(), text());
}

void SkinButton::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	// The press scale is common law (0.96 about the centre), painted as a
	// transform so the layout never moves.
	if (isDown() && isEnabled())
	{
		const QPointF center = QRectF(rect()).center();
		painter.translate(center);
		painter.scale(0.96, 0.96);
		painter.translate(-center);
	}

	DeviceButtonState state;
	state.text = text();
	state.primary = primary;
	state.enabled = isEnabled();
	state.pressed = isDown();
	state.focused = hasFocus();
	state.hover = isEnabled() ? hoverValue : 0.0;
	DeviceSkinPainter::active()->paintButton(painter, rect(), state, DeviceSkinPainter::activeTokens());
}

void SkinButton::enterEvent(QEnterEvent* event)
{
	animateHover(1.0, 150);
	QPushButton::enterEvent(event);
}

void SkinButton::leaveEvent(QEvent* event)
{
	animateHover(0.0, 110);
	QPushButton::leaveEvent(event);
}

void SkinButton::animateHover(double target, int duration)
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
