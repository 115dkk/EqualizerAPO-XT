/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Card frame for a command row. Paints its QSS chrome exactly like a plain
	QFrame and then lets the active skin paint per-command-type decoration on
	top (ISkin::paintCardChrome). The owning row keeps the embedded
	CommandRowInfo up to date as selection/enabled state changes.
*/

#pragma once

#include <QFrame>

#include "Editor/skins/ISkin.h"

class CommandRowFrame : public QFrame
{
	Q_OBJECT

public:
	explicit CommandRowFrame(QWidget* parent = nullptr);

	void applyRowInfo(const CommandRowInfo& info, QWidget* header);
	const CommandRowInfo& rowInfo() const;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	CommandRowInfo info;
};
