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

#include "BassManagementEditorDialog.h"

#include <algorithm>
#include <optional>
#include <variant>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

#include "BassManagement/Crossover.h"
#include "BassManagement/Preset.h"
#include "Editor/SkinManager.h"
#include "Editor/SkinTokens.h"
#include "Editor/widgets/DialogChrome.h"
#include "Editor/widgets/bassmanagement/BassManagementResponseView.h"
#include "Editor/widgets/bassmanagement/BassManagementUiModel.h"
#include "Editor/widgets/routing/BassManagementRoutingAdapter.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"

namespace
{
QString fromUtf8(const std::string& text)
{
	return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

const bassmgmt::Path* findPath(
	const bassmgmt::BassManagementState& state,
	const std::string& id)
{
	const auto path = std::find_if(state.paths.begin(), state.paths.end(),
		[&id](const bassmgmt::Path& candidate)
		{
			return candidate.id == id;
		});

	return path == state.paths.end() ? nullptr : &*path;
}

const bassmgmt::Path* sourceLfePath(
	const bassmgmt::BassManagementState& state)
{
	const auto path = std::find_if(state.paths.begin(), state.paths.end(),
		[](const bassmgmt::Path& candidate)
		{
			return candidate.kind == bassmgmt::PathKind::SourceLfe;
		});

	return path == state.paths.end() ? nullptr : &*path;
}

const bassmgmt::BiquadFilter* firstBiquad(
	const bassmgmt::Path& path,
	bassmgmt::BiquadType type)
{
	for (const bassmgmt::PathStage& stage : path.chain)
	{
		const bassmgmt::BiquadStage* biquad =
			std::get_if<bassmgmt::BiquadStage>(&stage);
		if (biquad != nullptr && biquad->filter.type == type)
			return &biquad->filter;
	}

	return nullptr;
}

std::optional<double> groupHighPass(
	const bassmgmt::BassManagementState& state,
	const bassmgmt::SpeakerGroup& group)
{
	for (const std::string& pathId : group.mainPathIds)
	{
		const bassmgmt::Path* path = findPath(state, pathId);
		if (path == nullptr)
			continue;

		const bassmgmt::BiquadFilter* filter =
			firstBiquad(*path, bassmgmt::BiquadType::HighPass);
		if (filter != nullptr)
			return filter->frequencyHz;
	}

	return std::nullopt;
}

std::optional<double> pathLowPass(const bassmgmt::Path& path)
{
	const bassmgmt::BiquadFilter* filter =
		firstBiquad(path, bassmgmt::BiquadType::LowPass);
	if (filter == nullptr)
		return std::nullopt;

	return filter->frequencyHz;
}

bool pathPolarity(const bassmgmt::Path& path)
{
	for (const bassmgmt::PathStage& stage : path.chain)
	{
		const bassmgmt::PolarityStage* polarity =
			std::get_if<bassmgmt::PolarityStage>(&stage);
		if (polarity != nullptr)
			return polarity->inverted;
	}

	return false;
}

double pathDelay(const bassmgmt::Path& path)
{
	for (const bassmgmt::PathStage& stage : path.chain)
	{
		const bassmgmt::DelayStage* delay =
			std::get_if<bassmgmt::DelayStage>(&stage);
		if (delay != nullptr)
			return delay->milliseconds;
	}

	return 0.0;
}

QString presetName(const bassmgmt::PresetDescriptor& preset)
{
	if (preset.id == bassmgmt::kIssue246FrontRear41PresetId)
		return BassManagementEditorDialog::tr(
			"Issue #246 - Front/Rear 4.1");

	return BassManagementEditorDialog::tr("%1")
		.arg(fromUtf8(preset.displayName));
}

QDoubleSpinBox* frequencySpinBox(QWidget* parent)
{
	QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
	spinBox->setDecimals(1);
	spinBox->setRange(10.0, 20000.0);
	spinBox->setSingleStep(1.0);
	spinBox->setSuffix(BassManagementEditorDialog::tr(" Hz"));
	// The crossover rows budget their width explicitly so frequency,
	// slope, delay and polarity all stay on one visible line.
	spinBox->setFixedWidth(110);
	return spinBox;
}

QDoubleSpinBox* delaySpinBox(QWidget* parent)
{
	QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
	spinBox->setDecimals(2);
	spinBox->setRange(0.0, 1000.0);
	spinBox->setSingleStep(0.1);
	spinBox->setSuffix(BassManagementEditorDialog::tr(" ms"));
	spinBox->setToolTip(BassManagementEditorDialog::tr(
		"Path delay applied after the crossover sections"));
	spinBox->setFixedWidth(100);
	return spinBox;
}

// The combo data encodes a supported recipe as order * 2 + alignment;
// kCustomSlope marks a section chain the recipe vocabulary cannot name
// (choosing it changes nothing - custom chains are preserved as written).
constexpr int kCustomSlope = -1;

int encodeSlope(const bassmgmt::CrossoverRecipe& recipe)
{
	return recipe.order * 2
		+ (recipe.alignment
			== bassmgmt::CrossoverAlignment::LinkwitzRiley
			? 1
			: 0);
}

bassmgmt::CrossoverRecipe decodeSlope(int encoded, double frequencyHz)
{
	bassmgmt::CrossoverRecipe recipe;
	recipe.alignment = (encoded & 1) != 0
		? bassmgmt::CrossoverAlignment::LinkwitzRiley
		: bassmgmt::CrossoverAlignment::Butterworth;
	recipe.order = encoded / 2;
	recipe.frequencyHz = frequencyHz;
	return recipe;
}

QComboBox* slopeComboBox(QWidget* parent)
{
	QComboBox* combo = new QComboBox(parent);
	const bassmgmt::CrossoverAlignment alignments[] = {
		bassmgmt::CrossoverAlignment::Butterworth,
		bassmgmt::CrossoverAlignment::LinkwitzRiley
	};
	for (const bassmgmt::CrossoverAlignment alignment : alignments)
	{
		for (int order = 2; order <= 8; order += 2)
		{
			bassmgmt::CrossoverRecipe recipe;
			recipe.alignment = alignment;
			recipe.order = order;
			combo->addItem(
				QStringLiteral("%1 (%2 dB/oct)")
					.arg(QString::fromStdString(
						bassmgmt::crossoverRecipeLabel(recipe)))
					.arg(bassmgmt::crossoverSlopeDbPerOctave(
						recipe)),
				encodeSlope(recipe));
		}
	}
	combo->addItem(
		BassManagementEditorDialog::tr("Custom"), kCustomSlope);
	combo->setToolTip(BassManagementEditorDialog::tr(
		"Crossover alignment and acoustic slope. Custom marks a "
		"hand-written section chain and leaves it untouched."));
	// Sized to the longest recipe label under the active skin's font: a
	// fixed width clipped "BW2 (12 dB/oct)" in the wider-glyph skins.
	combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	return combo;
}

void syncSlopeCombo(QComboBox* combo,
	const std::optional<bassmgmt::CrossoverRecipe>& recipe)
{
	const int customIndex = combo->findData(kCustomSlope);
	if (!recipe.has_value())
	{
		combo->setCurrentIndex(customIndex);
		return;
	}

	const int index = combo->findData(encodeSlope(*recipe));
	combo->setCurrentIndex(index >= 0 ? index : customIndex);
}

std::optional<bassmgmt::CrossoverRecipe> groupRecipe(
	const bassmgmt::BassManagementState& state,
	const bassmgmt::SpeakerGroup& group)
{
	for (const std::string& pathId : group.mainPathIds)
	{
		const bassmgmt::Path* path = findPath(state, pathId);
		if (path == nullptr)
			continue;
		return bassmgmt::recognizeCrossover(*path,
			bassmgmt::BiquadType::HighPass);
	}
	return std::nullopt;
}

std::optional<double> groupDelayMs(
	const bassmgmt::BassManagementState& state,
	const bassmgmt::SpeakerGroup& group)
{
	for (const std::string& pathId : group.mainPathIds)
	{
		const bassmgmt::Path* path = findPath(state, pathId);
		if (path == nullptr)
			continue;
		return pathDelay(*path);
	}
	return std::nullopt;
}

std::vector<std::wstring> bassPathTargets(
	const bassmgmt::BassManagementState& state)
{
	std::vector<std::wstring> result;

	for (const bassmgmt::Path& path : state.paths)
	{
		if (path.kind == bassmgmt::PathKind::Bass)
			result.emplace_back(path.id.begin(), path.id.end());
	}

	return result;
}

std::vector<std::wstring> physicalTargets(
	const bassmgmt::BassManagementState& state)
{
	std::vector<std::wstring> result;
	result.reserve(state.layout.channels.size());

	for (const bassmgmt::PhysicalChannel& channel : state.layout.channels)
		result.emplace_back(channel.id.begin(), channel.id.end());

	return result;
}
}

BassManagementEditorDialog::BassManagementEditorDialog(
	const bassmgmt::BassManagementState& initialState,
	unsigned deviceSampleRate,
	QWidget* parent)
	: QDialog(parent),
	  model(new BassManagementUiModel(
		  initialState, deviceSampleRate, this))
{
	setObjectName(QStringLiteral("BassManagementEditorDialog"));
	setWindowTitle(tr("Subwoofer Routing Editor"));
	// Wide enough that the label-sized routing matrices of a 4.1 state keep
	// every column on screen under every skin's fonts.
	resize(1360, 780);
	DialogChrome::attach(this);

	QVBoxLayout* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(10, 10, 10, 10);
	outerLayout->setSpacing(8);

	QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
	outerLayout->addWidget(splitter, 1);

	leftScroll = new QScrollArea(splitter);
	leftScroll->setWidgetResizable(true);
	leftScroll->setFrameShape(QFrame::NoFrame);
	// The pane scrolls vertically only. A horizontal bar here covered the
	// status line at the pane's bottom and cut rows off; instead the pane
	// claims the width its widest row needs (updateLeftPaneWidth).
	leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	QWidget* leftBody = new QWidget(leftScroll);
	QVBoxLayout* leftLayout = new QVBoxLayout(leftBody);
	leftLayout->setContentsMargins(0, 0, 6, 0);
	leftLayout->setSpacing(8);

	QGroupBox* layoutGroup =
		new QGroupBox(tr("Layout && preset"), leftBody);
	QFormLayout* layoutForm = new QFormLayout(layoutGroup);
	presetCombo = new QComboBox(layoutGroup);
	presetCombo->addItem(tr("Current state"), QString());

	for (const bassmgmt::PresetDescriptor& preset
		: bassmgmt::builtInPresets())
	{
		presetCombo->addItem(
			presetName(preset),
			fromUtf8(preset.id));
	}

	layoutForm->addRow(tr("Preset:"), presetCombo);
	leftLayout->addWidget(layoutGroup);

	QGroupBox* sourceLfeGroup =
		new QGroupBox(tr("Source LFE"), leftBody);
	QFormLayout* sourceLfeForm = new QFormLayout(sourceLfeGroup);

	sourceLfeGain = new QDoubleSpinBox(sourceLfeGroup);
	sourceLfeGain->setDecimals(1);
	sourceLfeGain->setRange(-60.0, 24.0);
	sourceLfeGain->setSingleStep(0.5);
	sourceLfeGain->setSuffix(tr(" dB"));
	sourceLfeForm->addRow(tr("Gain:"), sourceLfeGain);

	sourceLfePolarity = new QCheckBox(tr("Invert"), sourceLfeGroup);
	sourceLfeForm->addRow(tr("Polarity:"), sourceLfePolarity);

	sourceLfeDelay = new QDoubleSpinBox(sourceLfeGroup);
	sourceLfeDelay->setDecimals(2);
	sourceLfeDelay->setRange(0.0, 1000.0);
	sourceLfeDelay->setSingleStep(0.1);
	sourceLfeDelay->setSuffix(tr(" ms"));
	sourceLfeForm->addRow(tr("Delay:"), sourceLfeDelay);
	leftLayout->addWidget(sourceLfeGroup);

	QGroupBox* speakerGroupBox =
		new QGroupBox(tr("Speaker groups"), leftBody);
	groupForm = new QFormLayout(speakerGroupBox);
	leftLayout->addWidget(speakerGroupBox);

	QGroupBox* bassPathBox =
		new QGroupBox(tr("Bass paths"), leftBody);
	bassPathForm = new QFormLayout(bassPathBox);
	leftLayout->addWidget(bassPathBox);

	QGroupBox* headroomGroup =
		new QGroupBox(tr("Headroom"), leftBody);
	QFormLayout* headroomForm = new QFormLayout(headroomGroup);

	headroomAuto = new QCheckBox(tr("Automatic"), headroomGroup);
	headroomForm->addRow(tr("Mode:"), headroomAuto);

	manualTrim = new QDoubleSpinBox(headroomGroup);
	manualTrim->setDecimals(1);
	manualTrim->setRange(-60.0, 0.0);
	manualTrim->setSingleStep(0.5);
	manualTrim->setSuffix(tr(" dB"));
	headroomForm->addRow(tr("Manual trim:"), manualTrim);

	computedTrim = new QLabel(headroomGroup);
	headroomForm->addRow(tr("Applied trim:"), computedTrim);
	leftLayout->addWidget(headroomGroup);

	validationLabel = new QLabel(leftBody);
	validationLabel->setWordWrap(true);
	validationLabel->setObjectName(
		QStringLiteral("BassManagementValidationLabel"));
	leftLayout->addWidget(validationLabel);
	leftLayout->addStretch(1);

	leftScroll->setWidget(leftBody);
	splitter->addWidget(leftScroll);

	QWidget* rightBody = new QWidget(splitter);
	QVBoxLayout* rightLayout = new QVBoxLayout(rightBody);
	rightLayout->setContentsMargins(6, 0, 0, 0);
	rightLayout->setSpacing(8);

	QGroupBox* sendGroup =
		new QGroupBox(tr("Bass sends"), rightBody);
	bassSendRoutingLayout = new QVBoxLayout(sendGroup);
	rightLayout->addWidget(sendGroup, 1);

	QGroupBox* outputGroup =
		new QGroupBox(tr("Physical outputs"), rightBody);
	outputRoutingLayout = new QVBoxLayout(outputGroup);
	rightLayout->addWidget(outputGroup, 1);

	QGroupBox* responseGroup =
		new QGroupBox(tr("Path response"), rightBody);
	QVBoxLayout* responseLayout = new QVBoxLayout(responseGroup);
	responseView =
		new BassManagementResponseView(model, responseGroup);
	responseLayout->addWidget(responseView);
	rightLayout->addWidget(responseGroup, 2);

	splitter->addWidget(rightBody);
	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	// The crossover rows carry frequency + slope + delay (+ polarity), so
	// the form pane needs the width a single spin box column never did.
	splitter->setSizes({600, 740});

	buttonBox = new QDialogButtonBox(
		QDialogButtonBox::Ok
			| QDialogButtonBox::Cancel
			| QDialogButtonBox::Apply,
		this);
	outerLayout->addWidget(buttonBox);

	connect(presetCombo,
		qOverload<int>(&QComboBox::activated),
		this,
		&BassManagementEditorDialog::presetActivated);
	connect(sourceLfeGain,
		qOverload<double>(&QDoubleSpinBox::valueChanged),
		model,
		&BassManagementUiModel::setSourceLfeGainDb);
	connect(sourceLfePolarity, &QCheckBox::toggled,
		model, &BassManagementUiModel::setSourceLfePolarity);
	connect(sourceLfeDelay,
		qOverload<double>(&QDoubleSpinBox::valueChanged),
		model,
		&BassManagementUiModel::setSourceLfeDelayMs);
	connect(headroomAuto, &QCheckBox::toggled,
		model, &BassManagementUiModel::setHeadroomAuto);
	connect(manualTrim,
		qOverload<double>(&QDoubleSpinBox::valueChanged),
		model,
		&BassManagementUiModel::setManualTrimDb);

	connect(buttonBox, &QDialogButtonBox::accepted,
		this, &QDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected,
		this, &QDialog::reject);
	connect(buttonBox->button(QDialogButtonBox::Apply),
		&QPushButton::clicked,
		this,
		&BassManagementEditorDialog::applyClicked);

	connect(model, &BassManagementUiModel::stateEdited,
		this,
		[this]()
		{
			refreshControls();
			rebuildRoutingViews();
		});
	connect(model, &BassManagementUiModel::validationChanged,
		this, &BassManagementEditorDialog::refreshValidation);

	connect(SkinManager::instance(), &SkinManager::skinChanged,
		this,
		[this](const SkinTokens&)
		{
			rebuildRoutingViews();
			updateLeftPaneWidth();
			responseView->update();
		});

	rebuildFrequencyControls();
	refreshControls();
	rebuildRoutingViews();
	refreshValidation();
}

const bassmgmt::BassManagementState&
BassManagementEditorDialog::state() const
{
	return model->state();
}

void BassManagementEditorDialog::presetActivated(int index)
{
	const QString presetId =
		presetCombo->itemData(index).toString();
	if (presetId.isEmpty())
	{
		selectedPresetId.clear();
		return;
	}

	const QByteArray bytes = presetId.toUtf8();
	const bassmgmt::PresetCreateResult preset =
		bassmgmt::createBuiltInPreset(std::string_view(
			bytes.constData(), static_cast<std::size_t>(bytes.size())));
	if (!preset.succeeded())
		return;

	selectedPresetId = presetId;
	model->replaceState(*preset.state);
}

void BassManagementEditorDialog::bassSendRoutingEdited()
{
	if (bassSendRoutingView == nullptr)
		return;

	model->applyBassSendAssignments(
		bassSendRoutingView->assignments());
}

void BassManagementEditorDialog::outputRoutingEdited()
{
	if (outputRoutingView == nullptr)
		return;

	model->applyOutputAssignments(
		outputRoutingView->assignments());
}

void BassManagementEditorDialog::applyClicked()
{
	emit applied();
}

void BassManagementEditorDialog::refreshControls()
{
	const bassmgmt::BassManagementState& current = model->state();

	std::vector<std::string> expectedGroups;
	expectedGroups.reserve(current.speakerGroups.size());
	for (const bassmgmt::SpeakerGroup& group : current.speakerGroups)
		expectedGroups.push_back(group.id);

	std::vector<std::string> existingGroups;
	existingGroups.reserve(groupControls.size());
	for (const CrossoverControls& control : groupControls)
		existingGroups.push_back(control.id);

	std::vector<std::string> expectedBassPaths;
	for (const bassmgmt::Path& path : current.paths)
	{
		if (path.kind == bassmgmt::PathKind::Bass)
			expectedBassPaths.push_back(path.id);
	}

	std::vector<std::string> existingBassPaths;
	existingBassPaths.reserve(bassPathControls.size());
	for (const CrossoverControls& control : bassPathControls)
		existingBassPaths.push_back(control.id);

	if (expectedGroups != existingGroups
		|| expectedBassPaths != existingBassPaths)
	{
		rebuildFrequencyControls();
	}

	const QSignalBlocker presetBlocker(presetCombo);
	if (selectedPresetId.isEmpty())
	{
		presetCombo->setCurrentIndex(0);
	}
	else
	{
		const int index = presetCombo->findData(selectedPresetId);
		presetCombo->setCurrentIndex(index >= 0 ? index : 0);
	}

	const bassmgmt::Path* lfe = sourceLfePath(current);
	const bool hasSourceLfe = lfe != nullptr;
	sourceLfeGain->setEnabled(hasSourceLfe);
	sourceLfePolarity->setEnabled(hasSourceLfe);
	sourceLfeDelay->setEnabled(hasSourceLfe);

	if (lfe != nullptr)
	{
		const QSignalBlocker gainBlocker(sourceLfeGain);
		const QSignalBlocker polarityBlocker(sourceLfePolarity);
		const QSignalBlocker delayBlocker(sourceLfeDelay);
		sourceLfeGain->setValue(lfe->preGainDb);
		sourceLfePolarity->setChecked(pathPolarity(*lfe));
		sourceLfeDelay->setValue(pathDelay(*lfe));
	}

	for (CrossoverControls& control : groupControls)
	{
		const auto group = std::find_if(
			current.speakerGroups.begin(),
			current.speakerGroups.end(),
			[&control](const bassmgmt::SpeakerGroup& candidate)
			{
				return candidate.id == control.id;
			});
		if (group == current.speakerGroups.end())
			continue;

		const std::optional<double> frequency =
			groupHighPass(current, *group);
		control.frequency->setEnabled(frequency.has_value());
		if (frequency.has_value())
		{
			const QSignalBlocker blocker(control.frequency);
			control.frequency->setValue(*frequency);
		}

		{
			const QSignalBlocker blocker(control.slope);
			control.slope->setEnabled(frequency.has_value());
			syncSlopeCombo(control.slope,
				groupRecipe(current, *group));
		}

		const std::optional<double> delayMs =
			groupDelayMs(current, *group);
		control.delay->setEnabled(delayMs.has_value());
		if (delayMs.has_value())
		{
			const QSignalBlocker blocker(control.delay);
			control.delay->setValue(*delayMs);
		}
	}

	for (CrossoverControls& control : bassPathControls)
	{
		const bassmgmt::Path* path =
			findPath(current, control.id);
		if (path == nullptr)
			continue;

		const std::optional<double> frequency = pathLowPass(*path);
		control.frequency->setEnabled(frequency.has_value());
		if (frequency.has_value())
		{
			const QSignalBlocker blocker(control.frequency);
			control.frequency->setValue(*frequency);
		}

		{
			const QSignalBlocker blocker(control.slope);
			control.slope->setEnabled(frequency.has_value());
			syncSlopeCombo(control.slope,
				bassmgmt::recognizeCrossover(*path,
					bassmgmt::BiquadType::LowPass));
		}

		{
			const QSignalBlocker blocker(control.delay);
			control.delay->setValue(pathDelay(*path));
		}

		{
			const QSignalBlocker blocker(control.polarity);
			control.polarity->setChecked(pathPolarity(*path));
		}
	}

	const bool automatic =
		current.headroom.mode == bassmgmt::HeadroomMode::Auto;
	{
		const QSignalBlocker autoBlocker(headroomAuto);
		const QSignalBlocker trimBlocker(manualTrim);
		headroomAuto->setChecked(automatic);
		manualTrim->setValue(current.headroom.manualTrimDb);
	}
	manualTrim->setEnabled(!automatic);

	const std::optional<double> trim = model->computedTrimDb();
	computedTrim->setText(trim.has_value()
		? tr("%1 dB").arg(QString::number(*trim, 'f', 1))
		: tr("Unavailable"));
}

void BassManagementEditorDialog::rebuildFrequencyControls()
{
	while (groupForm->rowCount() > 0)
		groupForm->removeRow(0);
	while (bassPathForm->rowCount() > 0)
		bassPathForm->removeRow(0);

	groupControls.clear();
	bassPathControls.clear();

	const bassmgmt::BassManagementState& current = model->state();

	for (const bassmgmt::SpeakerGroup& group : current.speakerGroups)
	{
		QWidget* row = new QWidget(groupForm->parentWidget());
		QHBoxLayout* rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(6);

		CrossoverControls controls;
		controls.id = group.id;
		controls.frequency = frequencySpinBox(row);
		controls.frequency->setToolTip(
			tr("High-pass corner for this speaker group"));
		controls.slope = slopeComboBox(row);
		controls.delay = delaySpinBox(row);
		rowLayout->addWidget(controls.frequency);
		rowLayout->addWidget(controls.slope);
		rowLayout->addWidget(controls.delay);
		rowLayout->addStretch(1);

		groupForm->addRow(
			fromUtf8(group.displayName.empty()
				? group.id
				: group.displayName) + tr(" HP:"),
			row);

		const std::string groupId = group.id;
		connect(controls.frequency,
			qOverload<double>(&QDoubleSpinBox::valueChanged),
			this,
			[this, groupId](double frequencyHz)
			{
				model->setGroupHighPass(groupId, frequencyHz);
			});
		QComboBox* slope = controls.slope;
		QDoubleSpinBox* frequency = controls.frequency;
		connect(slope,
			qOverload<int>(&QComboBox::activated),
			this,
			[this, groupId, slope, frequency](int index)
			{
				const int encoded =
					slope->itemData(index).toInt();
				if (encoded == kCustomSlope)
					return;
				model->setGroupCrossover(groupId,
					decodeSlope(encoded, frequency->value()));
			});
		connect(controls.delay,
			qOverload<double>(&QDoubleSpinBox::valueChanged),
			this,
			[this, groupId](double milliseconds)
			{
				model->setGroupDelayMs(groupId, milliseconds);
			});

		groupControls.push_back(controls);
	}

	for (const bassmgmt::Path& path : current.paths)
	{
		if (path.kind != bassmgmt::PathKind::Bass)
			continue;

		QWidget* row = new QWidget(bassPathForm->parentWidget());
		QHBoxLayout* rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(6);

		CrossoverControls controls;
		controls.id = path.id;
		controls.frequency = frequencySpinBox(row);
		controls.frequency->setToolTip(
			tr("Low-pass corner for this bass path"));
		controls.slope = slopeComboBox(row);
		controls.delay = delaySpinBox(row);
		controls.polarity = new QCheckBox(tr("Invert"), row);
		controls.polarity->setToolTip(tr(
			"Invert the bass path's polarity (the phase flip a "
			"summed crossover often needs)"));
		rowLayout->addWidget(controls.frequency);
		rowLayout->addWidget(controls.slope);
		rowLayout->addWidget(controls.delay);
		rowLayout->addWidget(controls.polarity);
		rowLayout->addStretch(1);

		bassPathForm->addRow(fromUtf8(path.id) + tr(" LP:"), row);

		const std::string pathId = path.id;
		connect(controls.frequency,
			qOverload<double>(&QDoubleSpinBox::valueChanged),
			this,
			[this, pathId](double frequencyHz)
			{
				model->setBassPathLowPass(pathId, frequencyHz);
			});
		QComboBox* slope = controls.slope;
		QDoubleSpinBox* frequency = controls.frequency;
		connect(slope,
			qOverload<int>(&QComboBox::activated),
			this,
			[this, pathId, slope, frequency](int index)
			{
				const int encoded =
					slope->itemData(index).toInt();
				if (encoded == kCustomSlope)
					return;
				model->setBassPathCrossover(pathId,
					decodeSlope(encoded, frequency->value()));
			});
		connect(controls.delay,
			qOverload<double>(&QDoubleSpinBox::valueChanged),
			this,
			[this, pathId](double milliseconds)
			{
				model->setPathDelayMs(pathId, milliseconds);
			});
		connect(controls.polarity, &QCheckBox::toggled,
			this,
			[this, pathId](bool inverted)
			{
				model->setPathPolarity(pathId, inverted);
			});

		bassPathControls.push_back(controls);
	}

	updateLeftPaneWidth();
}

void BassManagementEditorDialog::updateLeftPaneWidth()
{
	// With the horizontal scrollbar off, the pane has to claim the width
	// its widest row needs under the active skin's fonts, or rows would
	// clip silently instead.
	if (leftScroll == nullptr || leftScroll->widget() == nullptr)
		return;

	const int contentWidth = leftScroll->widget()->sizeHint().width();
	const QScrollBar* verticalBar = leftScroll->verticalScrollBar();
	const int barWidth = verticalBar != nullptr
		? verticalBar->sizeHint().width()
		: 0;
	leftScroll->setMinimumWidth(contentWidth + barWidth + 4);
}

void BassManagementEditorDialog::rebuildRoutingViews()
{
	rebuildBassSendRoutingView();
	rebuildOutputRoutingView();
}

void BassManagementEditorDialog::rebuildBassSendRoutingView()
{
	if (bassSendRoutingView != nullptr)
	{
		bassSendRoutingLayout->removeWidget(bassSendRoutingView);
		bassSendRoutingView->hide();
		bassSendRoutingView->deleteLater();
		bassSendRoutingView = nullptr;
	}

	if (bassSendRoutingHint != nullptr)
	{
		bassSendRoutingLayout->removeWidget(bassSendRoutingHint);
		bassSendRoutingHint->hide();
		bassSendRoutingHint->deleteLater();
		bassSendRoutingHint = nullptr;
	}

	IRoutingRenderer* renderer =
		SkinManager::instance()->routingRenderer();
	if (renderer == nullptr)
	{
		bassSendRoutingHint = new QLabel(
			tr("The active heritage skin does not provide a routing editor."),
			bassSendRoutingLayout->parentWidget());
		bassSendRoutingHint->setWordWrap(true);
		bassSendRoutingLayout->addWidget(bassSendRoutingHint);
		return;
	}

	RoutingPortModel portModel;
	portModel.fixedSources =
		BassManagementRoutingAdapter::bassSendSources(model->state());
	portModel.allowFactors = false;

	bassSendRoutingView = renderer->create(
		BassManagementRoutingAdapter::toBassSendAssignments(
			model->state()),
		bassPathTargets(model->state()),
		portModel,
		bassSendRoutingLayout->parentWidget());
	bassSendRoutingLayout->addWidget(bassSendRoutingView);

	connect(bassSendRoutingView, &RoutingView::routingChanged,
		this,
		&BassManagementEditorDialog::bassSendRoutingEdited);
}

void BassManagementEditorDialog::rebuildOutputRoutingView()
{
	if (outputRoutingView != nullptr)
	{
		outputRoutingLayout->removeWidget(outputRoutingView);
		outputRoutingView->hide();
		outputRoutingView->deleteLater();
		outputRoutingView = nullptr;
	}

	if (outputRoutingHint != nullptr)
	{
		outputRoutingLayout->removeWidget(outputRoutingHint);
		outputRoutingHint->hide();
		outputRoutingHint->deleteLater();
		outputRoutingHint = nullptr;
	}

	IRoutingRenderer* renderer =
		SkinManager::instance()->routingRenderer();
	if (renderer == nullptr)
	{
		outputRoutingHint = new QLabel(
			tr("The active heritage skin does not provide a routing editor."),
			outputRoutingLayout->parentWidget());
		outputRoutingHint->setWordWrap(true);
		outputRoutingLayout->addWidget(outputRoutingHint);
		return;
	}

	RoutingPortModel portModel;
	portModel.fixedSources =
		BassManagementRoutingAdapter::outputSources(model->state());
	portModel.allowFactors = true;

	outputRoutingView = renderer->create(
		BassManagementRoutingAdapter::toOutputAssignments(
			model->state()),
		physicalTargets(model->state()),
		portModel,
		outputRoutingLayout->parentWidget());
	outputRoutingLayout->addWidget(outputRoutingView);

	connect(outputRoutingView, &RoutingView::routingChanged,
		this,
		&BassManagementEditorDialog::outputRoutingEdited);
}

void BassManagementEditorDialog::refreshValidation()
{
	const bassmgmt::ValidationResult& validation =
		model->validation();

	if (validation.diagnostics.empty())
	{
		validationLabel->setText(tr("State is valid."));
		return;
	}

	const auto error = std::find_if(
		validation.diagnostics.begin(),
		validation.diagnostics.end(),
		[](const bassmgmt::ValidationDiagnostic& diagnostic)
		{
			return diagnostic.severity
				== bassmgmt::DiagnosticSeverity::Error;
		});

	const bassmgmt::ValidationDiagnostic& diagnostic =
		error != validation.diagnostics.end()
			? *error
			: validation.diagnostics.front();

	validationLabel->setText(fromUtf8(diagnostic.message));
}
