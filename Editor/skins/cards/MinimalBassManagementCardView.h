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

class QEvent;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;

class MinimalBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit MinimalBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;
	void changeEvent(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void addReadoutRow(int row, const QString& caption,
		QLabel*& valueLabel, const QString& accessibleName,
		const QString& toolTip);
	void paintSeparator(QWidget* separator);
	void refreshElisions();
	void setElidedText(QLabel* label, const QString& fullText,
		const QString& toolTip = QString());
	void updateActionPresentation();

	QGridLayout* readoutGrid = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QWidget* readoutSeparator = nullptr;
	QWidget* actionRow = nullptr;
	QLabel* validityLabel = nullptr;
	QLabel* profileLabel = nullptr;
	QLabel* layoutValue = nullptr;
	QLabel* highPassValue = nullptr;
	QLabel* lowPassValue = nullptr;
	QLabel* lfeGainValue = nullptr;
	QLabel* trimValue = nullptr;
	QLabel* stageLabel = nullptr;
	QLabel* diagnosticLabel = nullptr;
	int actionButtonCount = 0;
};
