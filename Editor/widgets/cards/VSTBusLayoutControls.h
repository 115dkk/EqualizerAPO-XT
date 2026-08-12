/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Shared behavior for the VST3 main input/output bus contract. ISkin owns the
	presentation and returns a derived view through createVSTBusLayoutControls;
	this base owns values, focus order, accessibility, and signals only.
*/

#pragma once

#include <QColor>
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

protected:
	// Skin implementations call this overload and arrange the shared semantic
	// children in their own constitution-specific structure.
	explicit VSTBusLayoutControls(QWidget* parent, bool buildNeutralLayout);

	QLabel* inputCaptionLabel() const { return inputLabel; }
	QLabel* outputCaptionLabel() const { return outputLabel; }
	QComboBox* inputSelector() const { return inputCombo; }
	QComboBox* outputSelector() const { return outputCombo; }
	QLabel* directionIndicator() const { return directionLabel; }
	QLabel* statusTextLabel() const { return statusLabel; }
	QToolButton* removeLayoutsButton() const { return removeButton; }
	StatusTone statusTone() const { return currentTone; }
	QColor statusColor() const;

	virtual void statusPresentationChanged();

private:
	void buildNeutralLayout();
	static VST3BusLayout selectedLayout(const QComboBox* combo);
	static void selectLayout(QComboBox* combo, VST3BusLayout layout);

	QLabel* inputLabel = nullptr;
	QLabel* outputLabel = nullptr;
	QComboBox* inputCombo = nullptr;
	QComboBox* outputCombo = nullptr;
	QLabel* directionLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QToolButton* removeButton = nullptr;
	StatusTone currentTone = StatusTone::Neutral;
};
