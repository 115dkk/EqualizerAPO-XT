/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTIBILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include <string>
#include <vector>

#include <QDialog>

#include "SubwooferRouting/State.h"

class SubwooferRoutingResponseView;
class SubwooferRoutingUiModel;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class RoutingView;

class SubwooferRoutingEditorDialog : public QDialog
{
	Q_OBJECT

public:
	explicit SubwooferRoutingEditorDialog(
		const subroute::SubwooferRoutingState& state,
		unsigned deviceSampleRate,
		QWidget* parent = nullptr);

	const subroute::SubwooferRoutingState& state() const;

signals:
	void applied();

private slots:
	void presetActivated(int index);
	void bassSendRoutingEdited();
	void outputRoutingEdited();
	void applyClicked();

private:
	// One crossover row: corner frequency, alignment/slope recipe, path
	// delay, and (bass paths only) a polarity switch. The vocabulary a
	// practitioner's config actually uses - "LP 80 Hz LR4, 2.5 ms,
	// inverted send" - must be writable here, not just readable.
	struct CrossoverControls
	{
		std::string id;
		QDoubleSpinBox* frequency = nullptr;
		QComboBox* slope = nullptr;
		QDoubleSpinBox* delay = nullptr;
		QCheckBox* polarity = nullptr;
	};

	void refreshControls();
	void rebuildFrequencyControls();
	void updateLeftPaneWidth();
	void rebuildRoutingViews();
	void rebuildBassSendRoutingView();
	void rebuildOutputRoutingView();
	void refreshValidation();

	SubwooferRoutingUiModel* model = nullptr;
	QScrollArea* leftScroll = nullptr;
	QComboBox* presetCombo = nullptr;
	QDoubleSpinBox* sourceLfeGain = nullptr;
	QCheckBox* sourceLfePolarity = nullptr;
	QDoubleSpinBox* sourceLfeDelay = nullptr;
	QFormLayout* groupForm = nullptr;
	QFormLayout* bassPathForm = nullptr;
	std::vector<CrossoverControls> groupControls;
	std::vector<CrossoverControls> bassPathControls;
	QCheckBox* headroomAuto = nullptr;
	QDoubleSpinBox* manualTrim = nullptr;
	QLabel* computedTrim = nullptr;
	QLabel* validationLabel = nullptr;
	QVBoxLayout* bassSendRoutingLayout = nullptr;
	QVBoxLayout* outputRoutingLayout = nullptr;
	RoutingView* bassSendRoutingView = nullptr;
	RoutingView* outputRoutingView = nullptr;
	QLabel* bassSendRoutingHint = nullptr;
	QLabel* outputRoutingHint = nullptr;
	SubwooferRoutingResponseView* responseView = nullptr;
	QDialogButtonBox* buttonBox = nullptr;
	QString selectedPresetId;
};
