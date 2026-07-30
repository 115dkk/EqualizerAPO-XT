/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's bass-management card: a terminal-grid summary with an
	aggregate crosspoint board, coordinate readout and boxed mono facts.
*/

#pragma once

#include "Editor/widgets/cards/BassManagementCardView.h"

class MatrixBassManagementBoard;
class QHBoxLayout;
class QLabel;
class QPaintEvent;
class QResizeEvent;
class QWidget;

class MatrixBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit MatrixBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	QLabel* createReadoutCell(
		const QString& objectName,
		const QString& accessibleName,
		const QString& toolTip);
	void updateResponsiveVisibility();

	QWidget* summaryStrip = nullptr;
	QWidget* secondaryStrip = nullptr;
	QWidget* actionRow = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QLabel* validityCell = nullptr;
	QLabel* layoutCell = nullptr;
	QLabel* crossoverCell = nullptr;
	QLabel* pathsCell = nullptr;
	QLabel* sourceLfeCell = nullptr;
	QLabel* headroomCell = nullptr;
	QLabel* profileCell = nullptr;
	MatrixBassManagementBoard* board = nullptr;
	QLabel* coordinateLine = nullptr;
	QLabel* statusLine = nullptr;
};
