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

#include "BassManagement/State.h"

class BassManagementResponseView;
class BassManagementUiModel;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QVBoxLayout;
class RoutingView;

class BassManagementEditorDialog : public QDialog
{
	Q_OBJECT

public:
	explicit BassManagementEditorDialog(
		const bassmgmt::BassManagementState& state,
		unsigned deviceSampleRate,
		QWidget* parent = nullptr);

	const bassmgmt::BassManagementState& state() const;

signals:
	void applied();

private slots:
	void presetActivated(int index);
	void bassSendRoutingEdited();
	void outputRoutingEdited();
	void applyClicked();

private:
	struct FrequencyControl
	{
		std::string id;
		QDoubleSpinBox* spinBox = nullptr;
	};

	void refreshControls();
	void rebuildFrequencyControls();
	void rebuildRoutingViews();
	void rebuildBassSendRoutingView();
	void rebuildOutputRoutingView();
	void refreshValidation();

	BassManagementUiModel* model = nullptr;
	QComboBox* presetCombo = nullptr;
	QDoubleSpinBox* sourceLfeGain = nullptr;
	QCheckBox* sourceLfePolarity = nullptr;
	QDoubleSpinBox* sourceLfeDelay = nullptr;
	QFormLayout* groupForm = nullptr;
	QFormLayout* bassPathForm = nullptr;
	std::vector<FrequencyControl> groupControls;
	std::vector<FrequencyControl> bassPathControls;
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
	BassManagementResponseView* responseView = nullptr;
	QDialogButtonBox* buttonBox = nullptr;
	QString selectedPresetId;
};
