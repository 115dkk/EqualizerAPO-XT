/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "Editor/widgets/cards/SubwooferRoutingCardView.h"

class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QPaintEvent;

// Soft reads the subwoofer-routing state back as a sentence a consumer
// settings app would dare to show: one headline ("Bass below 80 Hz plays on
// the subwoofer."), one dim caption, and a short row of quiet fact pills.
// The review round retired the palette formula (HP/LP capsule arithmetic):
// a beginner could not read it, and this skin's tiebreaker removes any
// element that makes the screen more anxious.
class SoftSubwooferRoutingCardView : public SubwooferRoutingCardView
{
	Q_OBJECT

public:
	explicit SoftSubwooferRoutingCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const SubwooferRoutingCardState& state) override;
	void paintEvent(QPaintEvent* event) override;

private:
	QLabel* makeFactPill();

	QLabel* headlineLabel = nullptr;
	QLabel* captionLabel = nullptr;
	QLabel* layoutPill = nullptr;
	QLabel* lfePill = nullptr;
	QLabel* headroomPill = nullptr;
	QLabel* profilePill = nullptr;
	QHBoxLayout* actionLayout = nullptr;
};
