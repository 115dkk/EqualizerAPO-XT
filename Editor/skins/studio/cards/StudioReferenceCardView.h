/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio Glass's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): an identity line with lit-glass chips over a
	sunken mono data window.
	Constitution: docs/skins/studio.md ("참조 카드" section).
*/

#pragma once

#include <QList>

#include "Editor/widgets/cards/ReferenceCardView.h"

class ElidedLabel;
class QHBoxLayout;
class QLabel;

class StudioReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit StudioReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;

private:
	QHBoxLayout* identityLayout = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QHBoxLayout* windowLayout = nullptr;
	QWidget* busStrip = nullptr;
	ElidedLabel* nameLabel = nullptr;
	QLabel* formatChip = nullptr;
	QLabel* absChip = nullptr;
	QLabel* missingChip = nullptr;
	QWidget* windowPane = nullptr;
	ElidedLabel* locationLabel = nullptr;
	QLabel* factsLabel = nullptr;
	QWidget* statusRow = nullptr;
	QLabel* statusLamp = nullptr;
	QLabel* statusLabel = nullptr;
	QList<QAbstractButton*> actionButtons;
};
