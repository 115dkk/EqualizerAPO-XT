/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Push button whose pixels belong to the active skin's DeviceSkinPainter.
	The widget owns the feel that is common law across skins: interruptible
	hover progress (150ms in / 110ms out, OutCubic) and the 0.96 press scale
	painted about the centre - painters only decide what a button looks like,
	not how it moves.
*/

#pragma once

#include <QPushButton>

class QVariantAnimation;

class SkinButton : public QPushButton
{
	Q_OBJECT

public:
	SkinButton(const QString& text, bool primary, QWidget* parent = nullptr);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void animateHover(double target, int duration);

	bool primary;
	double hoverValue = 0.0;
	QVariantAnimation* hoverAnimation = nullptr;
};
