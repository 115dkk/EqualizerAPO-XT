/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include <QList>

#include "Editor/widgets/cards/BassManagementCardView.h"

class ElidedLabel;
class QAbstractButton;
class QEvent;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class SoftBassFlowWidget;
class SoftBassStatusChip;

class SoftBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit SoftBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void styleFactChip(QLabel* label, bool warning = false);
	void styleValidityChip(bool valid, bool hasError);
	void updateResponsiveVisibility();

	ElidedLabel* layoutLabel = nullptr;
	QLabel* validityChip = nullptr;
	ElidedLabel* crossoverLabel = nullptr;
	SoftBassFlowWidget* flowWidget = nullptr;
	QWidget* factRow = nullptr;
	QLabel* routeFact = nullptr;
	QLabel* sourceLfeFact = nullptr;
	QLabel* headroomFact = nullptr;
	ElidedLabel* profileFact = nullptr;
	SoftBassStatusChip* warningChip = nullptr;
	SoftBassStatusChip* errorChip = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QList<QAbstractButton*> actionButtons;
};
