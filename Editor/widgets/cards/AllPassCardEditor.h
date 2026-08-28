/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later

	The modern card for an all-pass filter.
*/

#pragma once

#include <QString>

#include "Editor/IFilterGUI.h"
#include "filters/BiQuadCommand.h"

class AudioKnob;
class EditableValue;
class QComboBox;
class QLabel;
class SegmentedControl;

// An all-pass gets its own card rather than the general biquad GUI, because
// almost nothing the general GUI offers applies to it. It has no gain - it is
// flat by construction - so a gain knob is a control that does nothing. Its
// centre frequency and width do not shape a level, they shape when frequencies
// arrive. And a card that says nothing about that leaves the filter looking
// like it does nothing at all, which is how it has read until now.
//
// What the card adds over the knobs: it names what the two values do in terms
// of phase, states outright that the magnitude is fixed, and offers to switch
// the analysis graph to a reading where the filter is visible.
//
// Value preservation is the rule the round trip depends on. A line written as
// "BW Oct 1" is saved as a bandwidth; a line written as "Q 10" is saved as a
// Q with its value untouched. The new default of 0.707 applies to filters this
// card creates, never to one it opens.
class AllPassCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	AllPassCardEditor(const BiQuadCommand& command, const QString& commandName, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void orderChanged(int index);
	void frequencyKnobChanged(int value);
	void frequencyValueChanged(double value);
	void widthKnobChanged(int value);
	void widthValueChanged(double value);
	void widthModeChanged(int index);

private:
	void setFrequency(double value, bool notify);
	void setWidth(double value, bool notify);
	bool bandwidthMode() const;
	bool firstOrder() const;
	void applyOrderVisibility();

	// The command keyword exactly as the line spelled it ("Filter", "Filter 1",
	// "Filter 99"), so editing a numbered line does not renumber it.
	QString originalCommand;

	AudioKnob* frequencyKnob = nullptr;
	EditableValue* frequencyValue = nullptr;
	AudioKnob* widthKnob = nullptr;
	EditableValue* widthValue = nullptr;
	QComboBox* widthModeCombo = nullptr;
	SegmentedControl* orderSegment = nullptr;
	QWidget* widthBlock = nullptr;
	QLabel* magnitudeNote = nullptr;
	SegmentedControl* graphSegment = nullptr;

	double currentFrequency = 1000.0;
	// Always in the unit the mode selector currently shows.
	double currentWidth = 0.0;
	bool synchronizing = false;
};
