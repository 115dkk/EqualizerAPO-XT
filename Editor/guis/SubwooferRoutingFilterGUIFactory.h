/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include "Editor/IFilterGUIFactory.h"

class FilterTable;

class SubwooferRoutingFilterGUIFactory : public IFilterGUIFactory
{
	Q_OBJECT

public:
	void initialize(FilterTable* filterTable) override;
	QList<FilterTemplate> createFilterTemplates() override;
	void startOfFile(const QString& configPath) override;
	IFilterGUI* createFilterGUI(QString& command, QString& parameters) override;

private:
	FilterTable* filterTable = nullptr;
	QString configPath;
	unsigned sampleRate = 48000;
	std::vector<std::wstring> deviceChannels;
};
