/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Compact, skin-neutral controls for the VST3 main input/output bus contract.
	The stock widgets deliberately inherit each skin's QSS; only semantic status
	text takes a token colour, and the status wording always carries the meaning.
*/

#pragma once

#include <QFrame>

#include "vst/VST3BusLayout.h"

class QComboBox;
class QLabel;
class QToolButton;

class VSTBusLayoutControls : public QFrame
{
	Q_OBJECT

public:
	enum class StatusTone
	{
		Neutral,
		Success,
		Warning,
		Critical
	};

	explicit VSTBusLayoutControls(QWidget* parent = nullptr);

	void setLayouts(VST3BusLayout input, VST3BusLayout output);
	void setControlsEnabled(bool enabled, const QString& disabledReason = QString());
	void setStatus(const QString& text, StatusTone tone, bool showRemoveAction = false);

signals:
	void layoutsEdited(VST3BusLayout input, VST3BusLayout output);
	void removeLayoutsRequested();

private slots:
	void selectionChanged();

private:
	static VST3BusLayout selectedLayout(const QComboBox* combo);
	static void selectLayout(QComboBox* combo, VST3BusLayout layout);

	QLabel* inputLabel = nullptr;
	QLabel* outputLabel = nullptr;
	QComboBox* inputCombo = nullptr;
	QComboBox* outputCombo = nullptr;
	QLabel* statusLabel = nullptr;
	QToolButton* removeButton = nullptr;
};
