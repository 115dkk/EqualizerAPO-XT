/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "ScalarKnobCardEditor.h"

class PreampCardEditor : public ScalarKnobCardEditor
{
	Q_OBJECT

public:
	explicit PreampCardEditor(double dbGain, QWidget* parent = nullptr);
	// Dynamic mode for a line whose gain is an inline `expression`: the knob
	// is powered down, the value position shows the expression as written (a
	// token, not a number), nothing ever re-serializes the line, and store()
	// reproduces the parameters verbatim. The raw editor stays the way to
	// change the expression; the analysis readouts show the computed value.
	explicit PreampCardEditor(const QString& dynamicParameters, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private slots:
	void knobChanged(int value);
	void valueChanged(double value);

private:
	void setGain(double value, bool notify);
	QString gainText() const;

	double currentGain = 0.0;
};
