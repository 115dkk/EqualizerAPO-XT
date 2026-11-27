/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Precision Minimal's reference card (Include / Convolution / MultiConvolution /
	VSTPlugin row bodies): one line of monospace type, reading

	  [channel] payload MISSING VST3 ABS location readout ! status ... BROWSE EDIT

	Constitution: docs/skins/minimal.md ("참조 카드" section).

	Colours live in precision_dark.qss / precision_light.qss (historic names);
	this class only builds structure and sets state-carrying properties. Long
	strings elide at paint time (ElidedLabel), never at set time.
*/

#pragma once

#include "Editor/widgets/cards/ReferenceCardView.h"

class ElidedLabel;
class QHBoxLayout;
class QLabel;

class MinimalReferenceCardView : public ReferenceCardView
{
	Q_OBJECT

public:
	explicit MinimalReferenceCardView(const QString& kind, QWidget* parent = nullptr);

	void addLeadingWidget(QWidget* widget) override;
	void placeBusStrip(QWidget* strip) override;
	// Told by MinimalSkin::prepareCommandRow when the line is a Modern row:
	// the payload keeps a floor of 12 characters there (the whole name when
	// it is shorter), so the location prefix elides first, and a loaded VST
	// unit with its bus expression prints on two lines. A card keeps its
	// present arrangement.
	void setInlineRow(bool inlineRow);

protected:
	void placeActionButton(ActionRole role, QAbstractButton* button) override;
	void applyState(const ReferenceCardState& state) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void restateCommandFloors();
	void restateNameFloor();
	void updateRowArrangement();

	bool inRow = false;
	bool twoLines = false;
	QHBoxLayout* pathLayout = nullptr;
	// The second printed line of a loaded VST unit in a row: where the unit
	// points (the location prefix) and how it is wired (the bus expression
	// and the measured readout). Empty, it takes no height.
	QHBoxLayout* wiringLayout = nullptr;
	QWidget* busStrip = nullptr;

	// The reference kind, kept for the command words: placeActionButton runs
	// before the first setState, so the state's kind is not there yet.
	QString cardKind;
	QHBoxLayout* lineLayout = nullptr;
	ElidedLabel* nameLabel = nullptr;
	QLabel* missingToken = nullptr;
	QLabel* checkingToken = nullptr;
	QLabel* formatToken = nullptr;
	QLabel* absToken = nullptr;
	ElidedLabel* dirLabel = nullptr;
	QLabel* readoutLabel = nullptr;
	ElidedLabel* statusLabel = nullptr;
	// Insertion cursor for leading widgets (MultiConvolution's channel combo
	// sits at the line head, before the payload).
	int leadingIndex = 0;
};
