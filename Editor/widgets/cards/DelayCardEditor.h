/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ScalarKnobCardEditor.h"

class QComboBox;

// The Delay card's modern body: knob + the unit selector (Time/Samples)
// standing as the caption over the editable value, mirroring the Preamp card.
// Replaces the legacy DelayFilterGUI in the card path; LegacyRows keeps the
// frozen .ui GUI.
class DelayCardEditor : public ScalarKnobCardEditor
{
	Q_OBJECT

public:
	explicit DelayCardEditor(double delay, bool isMs, QWidget* parent = nullptr);
	// Dynamic mode for a line whose delay is an inline `expression`: the
	// knob powers down, the unit selector hides (the unit lives inside the
	// as-written text), the value position shows the expression token, and
	// store() reproduces the parameters verbatim.
	explicit DelayCardEditor(const QString& dynamicParameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void knobChanged(int value);
	void valueChanged(double value);
	void unitChanged(int index);

private:
	void setDelay(double value, bool notify);
	QString delayText() const;

	QComboBox* unitCombo = nullptr;
	double currentDelay = 0.0;
	bool msMode = true;
};
