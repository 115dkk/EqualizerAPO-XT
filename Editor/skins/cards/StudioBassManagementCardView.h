/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <QList>

#include "Editor/widgets/cards/BassManagementCardView.h"

class ElidedLabel;
class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;

class StudioBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit StudioBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void updateResponsiveVisibility(int availableWidth);

	QHBoxLayout* actionLayout = nullptr;
	QLabel* validityChip = nullptr;
	ElidedLabel* layoutLabel = nullptr;
	ElidedLabel* profileLabel = nullptr;
	ElidedLabel* crossoverLabel = nullptr;
	QWidget* secondaryRow = nullptr;
	QLabel* groupChip = nullptr;
	QLabel* pathChip = nullptr;
	QLabel* routesChip = nullptr;
	QLabel* lfeLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QWidget* instrumentPane = nullptr;
	QWidget* headroomPane = nullptr;
	QLabel* headroomReadout = nullptr;
	QList<QAbstractButton*> actionButtons;
};
