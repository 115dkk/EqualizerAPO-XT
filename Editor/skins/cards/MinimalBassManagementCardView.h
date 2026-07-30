/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	EqualizerAPO-XT is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.
*/

#pragma once

#include "Editor/widgets/cards/BassManagementCardView.h"

class ElidedLabel;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPaintEvent;

class MinimalBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit MinimalBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void addReadoutRow(int row, const QString& caption,
		ElidedLabel*& valueLabel, const QString& accessibleName,
		const QString& toolTip);
	void paintSeparator(QWidget* separator);

	QGridLayout* readoutGrid = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QWidget* readoutSeparator = nullptr;
	QWidget* actionSeparator = nullptr;
	QWidget* actionRow = nullptr;
	QLabel* validityLabel = nullptr;
	ElidedLabel* profileLabel = nullptr;
	ElidedLabel* layoutValue = nullptr;
	ElidedLabel* highPassValue = nullptr;
	ElidedLabel* lowPassValue = nullptr;
	ElidedLabel* lfeGainValue = nullptr;
	ElidedLabel* trimValue = nullptr;
	ElidedLabel* stageLabel = nullptr;
	QLabel* diagnosticLabel = nullptr;
};
