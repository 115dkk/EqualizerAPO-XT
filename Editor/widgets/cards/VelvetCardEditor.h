/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "filters/VelvetCommand.h"

class QLabel;
class QToolButton;
class SegmentedControl;
class ValueScrubBox;

class VelvetCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	VelvetCardEditor(const VelvetCommand& command,
		const QString& validationError = QString(), QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private:
	QWidget* valueBlock(const QString& caption, ValueScrubBox*& box,
		double minimum, double maximum, double step, int decimals,
		const QString& suffix);
	void applyModeVisibility();
	void parametersChanged();
	void setAdvanced(bool expanded);

	VelvetCommand current;
	SegmentedControl* mode = nullptr;
	ValueScrubBox* amount = nullptr;
	ValueScrubBox* length = nullptr;
	ValueScrubBox* evolution = nullptr;
	ValueScrubBox* density = nullptr;
	ValueScrubBox* transition = nullptr;
	ValueScrubBox* decay = nullptr;
	ValueScrubBox* variation = nullptr;
	QWidget* evolutionBlock = nullptr;
	QWidget* transitionBlock = nullptr;
	QWidget* advancedPanel = nullptr;
	QToolButton* advancedToggle = nullptr;
	QLabel* validation = nullptr;
};
