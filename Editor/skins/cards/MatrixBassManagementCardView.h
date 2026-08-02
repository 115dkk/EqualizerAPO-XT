/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's bass-management card: a departure-board posting. Each
	fact lives in a boxed sunken mono cell under a mono board caption, the
	state cell is the only place traffic-light colour is spent, and faults
	are posted as a single remark line. The review round removed the
	miniature crosspoint board: an unlabeled grid of lit bars was a display
	nobody could read, and a board that cannot be read is not a board.
*/

#pragma once

#include "Editor/widgets/cards/BassManagementCardView.h"

class ElidedLabel;
class QHBoxLayout;
class QLabel;
class QPaintEvent;

class MatrixBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit MatrixBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;
	void paintEvent(QPaintEvent* event) override;

private:
	QWidget* makeReadoutColumn(const QString& caption, QLabel*& valueCell,
		const QString& accessibleName, const QString& toolTip);

	QHBoxLayout* actionLayout = nullptr;
	QLabel* stateCell = nullptr;
	QLabel* layoutCell = nullptr;
	QLabel* crossoverCell = nullptr;
	QLabel* lfeCell = nullptr;
	QLabel* trimCell = nullptr;
	ElidedLabel* profileCell = nullptr;
	QLabel* remarkLine = nullptr;
};
