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

class BassInstrumentWidget;
class GlowReadoutWidget;
class ProfileSummaryWidget;
class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class RightElidedLabel;

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
	RightElidedLabel* layoutLabel = nullptr;
	ProfileSummaryWidget* profileSummary = nullptr;
	RightElidedLabel* crossoverLabel = nullptr;
	QWidget* factsRow = nullptr;
	QLabel* routesChip = nullptr;
	QLabel* lfeChip = nullptr;
	QLabel* statusLabel = nullptr;
	BassInstrumentWidget* instrumentPane = nullptr;
	QWidget* headroomPane = nullptr;
	GlowReadoutWidget* headroomReadout = nullptr;
	QList<QAbstractButton*> actionButtons;
};
