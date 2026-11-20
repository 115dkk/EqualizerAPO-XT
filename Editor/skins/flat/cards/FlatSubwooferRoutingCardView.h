/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.
*/

#pragma once

#include "Editor/widgets/cards/SubwooferRoutingCardView.h"
#include "Editor/SkinTokens.h"

class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QPaintEvent;

// Stock Flat reads the subwoofer-routing state back as a sentence: one
// headline ("Bass below 80 Hz plays on the subwoofer."), one dim caption,
// and a short row of fact labels. The HP/LP capsule arithmetic of the other
// skins is not shown here: it is not a part a user already knows from other
// apps (docs/skins/flat.md, rule 1).
class FlatSubwooferRoutingCardView : public SubwooferRoutingCardView
{
	Q_OBJECT

public:
	explicit FlatSubwooferRoutingCardView(const SkinTokens& tokens, QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const SubwooferRoutingCardState& state) override;
	// No paintEvent: the view has no border of its own, so it paints no
	// focus. A click on it focuses the card, and the card frame shows the
	// focus (docs/skins/README.md, shared article 8).

private:
	const SkinTokens skinTokens;
	QLabel* makeFactPill();

	QLabel* headlineLabel = nullptr;
	QLabel* captionLabel = nullptr;
	QLabel* layoutPill = nullptr;
	QLabel* lfePill = nullptr;
	QLabel* headroomPill = nullptr;
	QLabel* profilePill = nullptr;
	QHBoxLayout* actionLayout = nullptr;
};
