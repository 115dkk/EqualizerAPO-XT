/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SkinButton.h"

#include <QPainter>

#include "platform/qt/HoverProgress.h"
#include "skins/DeviceSkinPainter.h"

SkinButton::SkinButton(const QString& text, bool primaryButton, QWidget* parent)
	: QPushButton(text, parent), primary(primaryButton)
{
	setCursor(Qt::PointingHandCursor);
	hover = new HoverProgress(this, [this]() {
		update();
	});
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
	state.hover = isEnabled() ? hover->value() : 0.0;
	DeviceSkinPainter::active()->paintButton(painter, rect(), state, DeviceSkinPainter::activeTokens());
}

void SkinButton::enterEvent(QEnterEvent* event)
{
	hover->animateTo(1.0, 150);
	QPushButton::enterEvent(event);
}

void SkinButton::leaveEvent(QEvent* event)
{
	hover->animateTo(0.0, 110);
	QPushButton::leaveEvent(event);
}
