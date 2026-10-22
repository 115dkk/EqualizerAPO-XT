/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2017 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QTabWidget>

// The main window's tab widget, carrying a DocumentTabBar.
class DocumentTabWidget : public QTabWidget
{
public:
	DocumentTabWidget(QWidget* parent = nullptr);
};
