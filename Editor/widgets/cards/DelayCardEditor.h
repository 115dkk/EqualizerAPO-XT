#pragma once

#include "Editor/IFilterGUI.h"

class AudioKnob;
class EditableValue;
class QComboBox;

// The Delay card's modern body: knob + the X5 unit selector (Time/Samples)
// standing as the caption over the editable value, mirroring the Preamp card.
// Replaces the legacy DelayFilterGUI in the card path, whose prose "Delay"
// label duplicated the card header and sat flush against the body's left
// edge (maintainer finding, dynamic-commands round 1). LegacyRows keeps the
// frozen .ui GUI.
class DelayCardEditor : public IFilterGUI
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
	void buildLayout(QWidget* valueWidget);
	void setDelay(double value, bool notify);
	QString delayText() const;

	AudioKnob* knob = nullptr;
	EditableValue* editableValue = nullptr;
	QComboBox* unitCombo = nullptr;
	double currentDelay = 0.0;
	bool msMode = true;
	bool updating = false;
	// Non-empty in dynamic mode: the as-written parameter text to reproduce.
	QString dynamicParameters;
};
