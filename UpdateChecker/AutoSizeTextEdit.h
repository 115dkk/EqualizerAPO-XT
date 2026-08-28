/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QTextEdit>

class AutoSizeTextEdit : public QTextEdit
{
	Q_OBJECT

public:
	AutoSizeTextEdit(QWidget* parent);
	~AutoSizeTextEdit();

	QSize sizeHint() const override;
};
