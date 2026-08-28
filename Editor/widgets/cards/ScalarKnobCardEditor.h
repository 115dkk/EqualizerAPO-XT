/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "Editor/IFilterGUI.h"

class AudioKnob;
class EditableValue;

// Shared interaction shell for a scalar card: one knob, one caption/value
// block, expression-token mode, and signal-safe synchronization.
class ScalarKnobCardEditor : public IFilterGUI
{
public:
	explicit ScalarKnobCardEditor(QWidget* parent = nullptr);

protected:
	void initializeScalarCard(const QString& editorObjectName,
		const QString& knobObjectName, const QString& valueBlockObjectName,
		QWidget* caption, EditableValue* value, const QString& dynamicParameters,
		bool bipolar, int knobMinimum, int knobMaximum);
	void synchronizeScalar(double value, int knobValue,
		const QString& knobText, bool notify);

	bool isSynchronizing() const { return synchronizing; }
	bool isDynamic() const { return !parametersAsWritten.isEmpty(); }
	const QString& dynamicParameters() const { return parametersAsWritten; }
	AudioKnob* scalarKnob() const { return knob; }
	EditableValue* scalarValue() const { return editableValue; }

private:
	AudioKnob* knob = nullptr;
	EditableValue* editableValue = nullptr;
	QString parametersAsWritten;
	bool synchronizing = false;
};
