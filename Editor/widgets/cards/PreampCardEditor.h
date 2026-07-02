#pragma once

#include "Editor/IFilterGUI.h"

class AudioKnob;
class EditableValue;

class PreampCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	explicit PreampCardEditor(double dbGain, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

	static double parseGain(const QString& parameters);

private slots:
	void knobChanged(int value);
	void valueChanged(double value);

private:
	void setGain(double value, bool notify);
	QString gainText() const;

	AudioKnob* knob = nullptr;
	EditableValue* editableValue = nullptr;
	double currentGain = 0.0;
	bool updating = false;
};
