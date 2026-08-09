/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	EqualizerAPO-XT is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "Editor/widgets/cards/SubwooferRoutingCardView.h"

class ElidedLabel;
class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QPaintEvent;

// Studio dresses the subwoofer-routing summary as captioned glass readouts:
// an identity line (validity chip, layout, profile), a sunken glass window
// holding the crossover / source-LFE / headroom readouts, and at most one
// quiet status line. The review round removed the response-trace instrument:
// theoretical filter curves carry no decision the user can make from the
// card (crossovers are chosen from measurements), and the skin's tiebreaker
// deletes anything that is neither arc, label nor value.
class StudioSubwooferRoutingCardView : public SubwooferRoutingCardView
{
	Q_OBJECT

public:
	explicit StudioSubwooferRoutingCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const SubwooferRoutingCardState& state) override;
	void paintEvent(QPaintEvent* event) override;

private:
	QWidget* makeReadoutCell(const QString& caption, QLabel*& valueLabel,
		bool primary, const QString& accessibleName, const QString& toolTip);

	QHBoxLayout* actionLayout = nullptr;
	QLabel* validityChip = nullptr;
	QLabel* layoutLabel = nullptr;
	ElidedLabel* profileLabel = nullptr;
	QLabel* crossoverValue = nullptr;
	QLabel* sourceLfeValue = nullptr;
	QLabel* headroomValue = nullptr;
	QLabel* statusLabel = nullptr;
};
