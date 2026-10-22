/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2017 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "DocumentTabBar.h"
#include "DocumentTabWidget.h"

DocumentTabWidget::DocumentTabWidget(QWidget* parent)
	: QTabWidget(parent)
{
	setTabBar(new DocumentTabBar(this));
}
