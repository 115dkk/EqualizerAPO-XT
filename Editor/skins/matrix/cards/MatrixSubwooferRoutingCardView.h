/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Signal Matrix's subwoofer-routing card: a departure-board posting. Each
	fact lives in a boxed sunken mono cell under a mono board caption, the
	state cell is the only place traffic-light colour is spent, and faults
	are posted as a single remark line. The review round removed the
	miniature crosspoint board: an unlabeled grid of lit bars was a display
	nobody could read, and a board that cannot be read is not a board.
*/

#pragma once

#include "Editor/widgets/cards/SubwooferRoutingCardView.h"
#include "Editor/SkinTokens.h"

class ElidedLabel;
class QHBoxLayout;
class QLabel;
class QPaintEvent;

class MatrixSubwooferRoutingCardView : public SubwooferRoutingCardView
{
	Q_OBJECT

public:
	explicit MatrixSubwooferRoutingCardView(const SkinTokens& tokens, QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const SubwooferRoutingCardState& state) override;
	void paintEvent(QPaintEvent* event) override;

private:
	const SkinTokens skinTokens;
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
