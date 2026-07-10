#pragma once

#include "Editor/IFilterGUI.h"

class AudioKnob;
class EditableValue;

class PreampCardEditor : public IFilterGUI
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

	static double parseGain(const QString& parameters);

private slots:
	void knobChanged(int value);
	void valueChanged(double value);

private:
	void buildLayout(QWidget* valueWidget);
	void setGain(double value, bool notify);
	QString gainText() const;

	AudioKnob* knob = nullptr;
	EditableValue* editableValue = nullptr;
	double currentGain = 0.0;
	bool updating = false;
	// Non-empty in dynamic mode: the as-written parameter text to reproduce.
	QString dynamicParameters;
};
