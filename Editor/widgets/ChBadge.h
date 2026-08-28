/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QWidget>

class ChBadge : public QWidget
{
	Q_OBJECT

public:
	explicit ChBadge(QWidget* parent = nullptr);
	explicit ChBadge(const QString& channel, QWidget* parent = nullptr);

	const QString& channel() const;
	void setChannel(const QString& channel);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;

private:
	QColor channelColor() const;
	bool isVirtualChannel() const;

	QString currentChannel;
};
