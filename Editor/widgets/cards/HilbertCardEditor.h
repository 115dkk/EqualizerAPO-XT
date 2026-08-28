/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "filters/HilbertCommand.h"

class ChannelRoleSelector;
class QLabel;
class SegmentedControl;

class HilbertCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	HilbertCardEditor(const HilbertCommand& command, unsigned sampleRate,
		const std::vector<std::wstring>& deviceChannels = {},
		const QString& validationError = QString(), QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void configureChannels(std::vector<std::wstring>& channelNames) override;

private:
	void changed();
	void refreshReadouts();

	HilbertCommand current;
	unsigned sampleRate = 48000;
	std::vector<std::wstring> deviceChannels;
	ChannelRoleSelector* shiftedSelector = nullptr;
	ChannelRoleSelector* alignedSelector = nullptr;
	SegmentedControl* direction = nullptr;
	SegmentedControl* graph = nullptr;
	QLabel* latencyValue = nullptr;
	QLabel* phaseValue = nullptr;
	QLabel* validation = nullptr;
};
