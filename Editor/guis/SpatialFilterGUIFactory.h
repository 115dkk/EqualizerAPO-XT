/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include "Editor/IFilterGUIFactory.h"

class SpatialFilterGUIFactory : public IFilterGUIFactory
{
	Q_OBJECT

public:
	void initialize(FilterTable* filterTable) override;
	QList<FilterTemplate> createFilterTemplates() override;
	IFilterGUI* createFilterGUI(QString& command, QString& parameters) override;

private:
	unsigned sampleRate = 48000;
	std::vector<std::wstring> deviceChannels;
};
