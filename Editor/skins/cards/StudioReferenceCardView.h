/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Studio Glass's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): the identity stands in the light, the facts sit
	behind sunken glass. One identity line carries the name (the row's
	brightest ink) and its lit-glass-chip badges; below it a recessed mono
	window holds the location and the measured readout as data behind glass.
	A broken reference dims the name and lights a small danger chip instead
	of shouting; status is a tiny severity lamp next to one quiet line.
	See docs/skins/studio.md for the constitution this answers to.
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

	void addActionButton(ActionRole role, QAbstractButton* button) override;
	void addLeadingWidget(QWidget* widget) override;

protected:
	void applyState(const ReferenceCardState& state) override;

private:
	QHBoxLayout* identityLayout = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	ElidedLabel* nameLabel = nullptr;
	QLabel* formatChip = nullptr;
	QLabel* absChip = nullptr;
	QLabel* missingChip = nullptr;
	QWidget* windowPane = nullptr;
	QLabel* locationGlyph = nullptr;
	ElidedLabel* locationLabel = nullptr;
	QLabel* factsLabel = nullptr;
	QWidget* statusRow = nullptr;
	QLabel* statusLamp = nullptr;
	QLabel* statusLabel = nullptr;
	QAbstractButton* browseButton = nullptr;
	QList<QAbstractButton*> actionButtons;
};
