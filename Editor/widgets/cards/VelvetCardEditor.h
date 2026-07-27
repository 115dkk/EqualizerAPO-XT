#pragma once

#include <string>
#include <vector>

#include "Editor/IFilterGUI.h"
#include "filters/VelvetCommand.h"

class QLabel;
class QToolButton;
class SegmentedControl;
class ValueScrubBox;
class VelvetImpulsePreview;

class VelvetCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	VelvetCardEditor(const VelvetCommand& command, unsigned sampleRate,
		const QString& validationError = QString(), QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;
	void configureChannels(std::vector<std::wstring>& channelNames) override;

private:
	QWidget* valueBlock(const QString& caption, ValueScrubBox*& box,
		double minimum, double maximum, double step, int decimals,
		const QString& suffix);
	void applyModeVisibility();
	void parametersChanged();
	void refreshPreview();
	void setAdvanced(bool expanded);

	VelvetCommand current;
	unsigned sampleRate = 48000;
	unsigned channelCount = 2;
	SegmentedControl* mode = nullptr;
	ValueScrubBox* amount = nullptr;
	ValueScrubBox* length = nullptr;
	ValueScrubBox* evolution = nullptr;
	ValueScrubBox* density = nullptr;
	ValueScrubBox* transition = nullptr;
	ValueScrubBox* decay = nullptr;
	ValueScrubBox* variation = nullptr;
	QWidget* evolutionBlock = nullptr;
	QWidget* transitionBlock = nullptr;
	QWidget* advancedPanel = nullptr;
	QToolButton* advancedToggle = nullptr;
	QLabel* statistics = nullptr;
	QLabel* validation = nullptr;
	VelvetImpulsePreview* preview = nullptr;
};
