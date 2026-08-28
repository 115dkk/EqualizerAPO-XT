/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The troubleshooting disclosure's header row: a full-width, keyboard
	reachable fold control painted by the active skin's DeviceSkinPainter.
	The slide animation of the panel below stays in
	DeviceSelector::onTroubleShootingToggled.
*/

#pragma once

#include <QWidget>

class QVariantAnimation;

class DisclosureHeader : public QWidget
{
	Q_OBJECT

public:
	explicit DisclosureHeader(QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setChecked(bool open);
	bool isChecked() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void toggled(bool open);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	void animateHover(double target, int duration);

	QString title;
	bool open = false;
	double hoverValue = 0.0;
	QVariantAnimation* hoverAnimation = nullptr;
};
