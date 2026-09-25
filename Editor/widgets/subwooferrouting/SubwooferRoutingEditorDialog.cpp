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

#include "SubwooferRoutingEditorDialog.h"

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
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "SubwooferRouting/Crossover.h"
#include "SubwooferRouting/Preset.h"
#include "Editor/SkinManager.h"
#include "Editor/SkinTokens.h"
#include "Editor/widgets/DialogChrome.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingResponseView.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingStateReads.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingUiModel.h"
#include "Editor/widgets/routing/SubwooferRoutingRoutingAdapter.h"
#include "Editor/widgets/routing/IRoutingRenderer.h"

using subwooferroutingeditor::findPath;
using subwooferroutingeditor::groupDelayMs;
using subwooferroutingeditor::groupHighPass;
using subwooferroutingeditor::groupRecipe;
using subwooferroutingeditor::pathDelayMs;
using subwooferroutingeditor::pathLowPass;
using subwooferroutingeditor::pathPolarity;
using subwooferroutingeditor::sourceLfeAdjustmentDb;
using subwooferroutingeditor::sourceLfePath;

namespace
{
QString fromUtf8(const std::string& text)
{
	return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

// The same name the card's preset menu shows (audit #348).
QString presetName(const subroute::PresetDescriptor& preset)
{
	return fromUtf8(subwooferroutingeditor::presetDisplayName(preset));
}

QDoubleSpinBox* frequencySpinBox(QWidget* parent)
{
	QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
	spinBox->setDecimals(1);
	spinBox->setRange(10.0, 20000.0);
	spinBox->setSingleStep(1.0);
	spinBox->setSuffix(SubwooferRoutingEditorDialog::tr(" Hz"));
	// The crossover rows budget their width explicitly so frequency,
	// slope, delay and polarity all stay on one visible line.
	spinBox->setFixedWidth(124);
	return spinBox;
}

QDoubleSpinBox* delaySpinBox(QWidget* parent)
{
	QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
	spinBox->setDecimals(2);
	spinBox->setRange(0.0, 1000.0);
	spinBox->setSingleStep(0.1);
	spinBox->setSuffix(SubwooferRoutingEditorDialog::tr(" ms"));
	spinBox->setToolTip(SubwooferRoutingEditorDialog::tr(
		"Path delay applied after the crossover sections"));
	spinBox->setFixedWidth(112);
	return spinBox;
}

// The combo data encodes a supported recipe as order * 2 + alignment;
// kCustomSlope marks a section chain the recipe vocabulary cannot name
// (choosing it changes nothing - custom chains are preserved as written).
constexpr int kCustomSlope = -1;

int encodeSlope(const subroute::CrossoverRecipe& recipe)
{
	return recipe.order * 2
		+ (recipe.alignment
			== subroute::CrossoverAlignment::LinkwitzRiley
			? 1
			: 0);
}

subroute::CrossoverRecipe decodeSlope(int encoded, double frequencyHz)
{
	subroute::CrossoverRecipe recipe;
	recipe.alignment = (encoded & 1) != 0
		? subroute::CrossoverAlignment::LinkwitzRiley
		: subroute::CrossoverAlignment::Butterworth;
	recipe.order = encoded / 2;
	recipe.frequencyHz = frequencyHz;
	return recipe;
}

QComboBox* slopeComboBox(QWidget* parent)
{
	QComboBox* combo = new QComboBox(parent);
	const subroute::CrossoverAlignment alignments[] = {
		subroute::CrossoverAlignment::Butterworth,
		subroute::CrossoverAlignment::LinkwitzRiley
	};
	for (const subroute::CrossoverAlignment alignment : alignments)
	{
		for (int order = 2; order <= 8; order += 2)
		{
			subroute::CrossoverRecipe recipe;
			recipe.alignment = alignment;
			recipe.order = order;
			combo->addItem(
				QStringLiteral("%1 (%2 dB/oct)")
					.arg(QString::fromStdString(
						subroute::crossoverRecipeLabel(recipe)))
					.arg(subroute::crossoverSlopeDbPerOctave(
						recipe)),
				encodeSlope(recipe));
		}
	}
	combo->addItem(
		SubwooferRoutingEditorDialog::tr("Custom"), kCustomSlope);
	combo->setToolTip(SubwooferRoutingEditorDialog::tr(
		"Crossover alignment and acoustic slope. Custom marks a "
		"hand-written section chain and leaves it untouched."));
	// Sized to the longest recipe label under the active skin's font: a
	// fixed width clipped "BW2 (12 dB/oct)" in the wider-glyph skins.
	combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	return combo;
}

void syncSlopeCombo(QComboBox* combo,
	const std::optional<subroute::CrossoverRecipe>& recipe)
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

std::vector<std::wstring> bassPathTargets(
	const subroute::SubwooferRoutingState& state)
{
	std::vector<std::wstring> result;

	for (const subroute::Path& path : state.paths)
	{
		if (path.kind == subroute::PathKind::Bass)
			result.emplace_back(path.id.begin(), path.id.end());
	}

	return result;
}

std::vector<std::wstring> physicalTargets(
	const subroute::SubwooferRoutingState& state)
{
	std::vector<std::wstring> result;
	result.reserve(state.layout.channels.size());

	for (const subroute::PhysicalChannel& channel : state.layout.channels)
		result.emplace_back(channel.id.begin(), channel.id.end());

	return result;
}
}

SubwooferRoutingEditorDialog::SubwooferRoutingEditorDialog(
	const subroute::SubwooferRoutingState& initialState,
	unsigned deviceSampleRate,
	QWidget* parent)
	: QDialog(parent),
	  model(new SubwooferRoutingUiModel(
		  initialState, deviceSampleRate, this))
{
	setObjectName(QStringLiteral("SubwooferRoutingEditorDialog"));
	setWindowTitle(tr("Subwoofer Routing Editor"));
	// Wide enough that the label-sized routing matrices of a 4.1 state keep
	// every column on screen under every skin's fonts.
	resize(1360, 810);
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

	for (const subroute::PresetDescriptor& preset
		: subroute::builtInPresets())
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
	sourceLfeForm->addRow(tr("LFE gain adjustment:"), sourceLfeGain);

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
		QStringLiteral("SubwooferRoutingValidationLabel"));
	leftLayout->addWidget(validationLabel);
	leftLayout->addStretch(1);

	leftScroll->setWidget(leftBody);
	splitter->addWidget(leftScroll);

	// Routing views can reveal every seeded Copy channel. Keep their changing
	// minimum height inside a scroll area so an expanded matrix cannot push the
	// response plot and dialog buttons below the available desktop. When the
	// views fold again, widgetResizable lets the response plot use the freed
	// viewport instead of retaining the expanded content height.
	QScrollArea* rightScroll = new QScrollArea(splitter);
	rightScroll->setObjectName(
		QStringLiteral("SubwooferRoutingContentScroll"));
	rightScroll->setWidgetResizable(true);
	rightScroll->setFrameShape(QFrame::NoFrame);
	rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	rightScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

	QWidget* rightBody = new QWidget(rightScroll);
	rightBody->setSizePolicy(
		QSizePolicy::Preferred, QSizePolicy::Minimum);
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
		new SubwooferRoutingResponseView(model, responseGroup);
	responseLayout->addWidget(responseView);
	rightLayout->addWidget(responseGroup, 2);

	rightScroll->setWidget(rightBody);
	splitter->addWidget(rightScroll);
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
	buttonBox->setObjectName(
		QStringLiteral("SubwooferRoutingButtonBox"));
	// Copy's inline channel editor commits with Enter. A dialog default button
	// sees the same key and used to accept the whole editor immediately after
	// that commit. Keep all actions explicit; keyboard users can still tab to a
	// button and activate it with Space.
	for (QAbstractButton* abstractButton : buttonBox->buttons())
	{
		QPushButton* pushButton =
			qobject_cast<QPushButton*>(abstractButton);
		if (pushButton == nullptr)
			continue;
		pushButton->setAutoDefault(false);
		pushButton->setDefault(false);
	}
	outerLayout->addWidget(buttonBox);

	connect(presetCombo,
		qOverload<int>(&QComboBox::activated),
		this,
		&SubwooferRoutingEditorDialog::presetActivated);
	connect(sourceLfeGain,
		qOverload<double>(&QDoubleSpinBox::valueChanged),
		model,
		&SubwooferRoutingUiModel::setSourceLfeGainDb);
	connect(sourceLfePolarity, &QCheckBox::toggled,
		model, &SubwooferRoutingUiModel::setSourceLfePolarity);
	connect(sourceLfeDelay,
		qOverload<double>(&QDoubleSpinBox::valueChanged),
		model,
		&SubwooferRoutingUiModel::setSourceLfeDelayMs);
	connect(headroomAuto, &QCheckBox::toggled,
		model, &SubwooferRoutingUiModel::setHeadroomAuto);
	connect(manualTrim,
		qOverload<double>(&QDoubleSpinBox::valueChanged),
		model,
		&SubwooferRoutingUiModel::setManualTrimDb);

	connect(buttonBox, &QDialogButtonBox::accepted,
		this, &QDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected,
		this, &QDialog::reject);
	connect(buttonBox->button(QDialogButtonBox::Apply),
		&QPushButton::clicked,
		this,
		&SubwooferRoutingEditorDialog::applyClicked);

	connect(model, &SubwooferRoutingUiModel::stateEdited,
		this,
		[this]()
		{
			refreshControls();
			rebuildRoutingViews();
		});
	connect(model, &SubwooferRoutingUiModel::validationChanged,
		this, &SubwooferRoutingEditorDialog::refreshValidation);

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
	// Do not let the button box become the dialog's initial focus target.
	// Starting on the preset field also makes the first keyboard action part of
	// the editor rather than an accidental confirmation.
	presetCombo->setObjectName(
		QStringLiteral("SubwooferRoutingPresetCombo"));
	presetCombo->setFocus(Qt::OtherFocusReason);
}

const subroute::SubwooferRoutingState&
SubwooferRoutingEditorDialog::state() const
{
	return model->state();
}

void SubwooferRoutingEditorDialog::keyPressEvent(QKeyEvent* event)
{
	// Inline Copy editors handle Return first and then disappear while
	// committing their value. Qt can propagate that same key press to the
	// dialog after focus has moved, where QDialog would activate its main
	// button even though the button is not auto-default. Consume only that
	// dialog-level Return/Enter; child editors still receive and handle it.
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
	{
		event->accept();
		return;
	}

	QDialog::keyPressEvent(event);
}

void SubwooferRoutingEditorDialog::presetActivated(int index)
{
	const QString presetId =
		presetCombo->itemData(index).toString();
	if (presetId.isEmpty())
	{
		selectedPresetId.clear();
		return;
	}

	const QByteArray bytes = presetId.toUtf8();
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(std::string_view(
			bytes.constData(), static_cast<std::size_t>(bytes.size())));
	if (!preset.succeeded())
		return;

	selectedPresetId = presetId;
	model->replaceState(*preset.state);
}

void SubwooferRoutingEditorDialog::bassSendRoutingEdited()
{
	if (bassSendRoutingView == nullptr)
		return;

	model->applyBassSendAssignments(
		bassSendRoutingView->assignments());
}

void SubwooferRoutingEditorDialog::outputRoutingEdited()
{
	if (outputRoutingView == nullptr)
		return;

	model->applyOutputAssignments(
		outputRoutingView->assignments());
}

void SubwooferRoutingEditorDialog::applyClicked()
{
	emit applied();
}

void SubwooferRoutingEditorDialog::refreshControls()
{
	const subroute::SubwooferRoutingState& current = model->state();

	std::vector<std::string> expectedGroups;
	expectedGroups.reserve(current.speakerGroups.size());
	for (const subroute::SpeakerGroup& group : current.speakerGroups)
		expectedGroups.push_back(group.id);

	std::vector<std::string> existingGroups;
	existingGroups.reserve(groupControls.size());
	for (const CrossoverControls& control : groupControls)
		existingGroups.push_back(control.id);

	std::vector<std::string> expectedBassPaths;
	for (const subroute::Path& path : current.paths)
	{
		if (path.kind == subroute::PathKind::Bass)
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

	const subroute::Path* lfe = sourceLfePath(current);
	const bool hasSourceLfe = lfe != nullptr;
	sourceLfeGain->setEnabled(hasSourceLfe);
	sourceLfePolarity->setEnabled(hasSourceLfe);
	sourceLfeDelay->setEnabled(hasSourceLfe);

	if (lfe != nullptr)
	{
		const QSignalBlocker gainBlocker(sourceLfeGain);
		const QSignalBlocker polarityBlocker(sourceLfePolarity);
		const QSignalBlocker delayBlocker(sourceLfeDelay);
		sourceLfeGain->setValue(sourceLfeAdjustmentDb(*lfe));
		sourceLfePolarity->setChecked(pathPolarity(*lfe));
		sourceLfeDelay->setValue(pathDelayMs(*lfe));
	}

	for (CrossoverControls& control : groupControls)
	{
		const auto group = std::find_if(
			current.speakerGroups.begin(),
			current.speakerGroups.end(),
			[&control](const subroute::SpeakerGroup& candidate)
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
		const subroute::Path* path =
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
				subroute::recognizeCrossover(*path,
					subroute::BiquadType::LowPass));
		}

		{
			const QSignalBlocker blocker(control.delay);
			control.delay->setValue(pathDelayMs(*path));
		}

		{
			const QSignalBlocker blocker(control.polarity);
			control.polarity->setChecked(pathPolarity(*path));
		}
	}

	const bool automatic =
		current.headroom.mode == subroute::HeadroomMode::Auto;
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

void SubwooferRoutingEditorDialog::rebuildFrequencyControls()
{
	while (groupForm->rowCount() > 0)
		groupForm->removeRow(0);
	while (bassPathForm->rowCount() > 0)
		bassPathForm->removeRow(0);

	groupControls.clear();
	bassPathControls.clear();

	const subroute::SubwooferRoutingState& current = model->state();

	for (const subroute::SpeakerGroup& group : current.speakerGroups)
	{
		// Add the layout directly to QFormLayout. A wrapper QWidget would paint
		// the skin's global window background across the unused stretch at the
		// right of the controls, leaving a dark rectangular "crumb" inside the
		// group-box surface.
		QWidget* rowParent = groupForm->parentWidget();
		QHBoxLayout* rowLayout = new QHBoxLayout;
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(6);

		CrossoverControls controls;
		controls.id = group.id;
		controls.frequency = frequencySpinBox(rowParent);
		controls.frequency->setToolTip(
			tr("High-pass corner for this speaker group"));
		controls.slope = slopeComboBox(rowParent);
		controls.delay = delaySpinBox(rowParent);
		rowLayout->addWidget(controls.frequency);
		rowLayout->addWidget(controls.slope);
		rowLayout->addWidget(controls.delay);
		rowLayout->addStretch(1);

		groupForm->addRow(
			fromUtf8(group.displayName.empty()
				? group.id
				: group.displayName) + tr(" HP:"),
			rowLayout);

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

	for (const subroute::Path& path : current.paths)
	{
		if (path.kind != subroute::PathKind::Bass)
			continue;

		QWidget* rowParent = bassPathForm->parentWidget();
		QHBoxLayout* rowLayout = new QHBoxLayout;
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(6);

		CrossoverControls controls;
		controls.id = path.id;
		controls.frequency = frequencySpinBox(rowParent);
		controls.frequency->setToolTip(
			tr("Low-pass corner for this bass path"));
		controls.slope = slopeComboBox(rowParent);
		controls.delay = delaySpinBox(rowParent);
		controls.polarity = new QCheckBox(tr("Invert"), rowParent);
		controls.polarity->setToolTip(tr(
			"Invert the bass path's polarity (the phase flip a "
			"summed crossover often needs)"));
		rowLayout->addWidget(controls.frequency);
		rowLayout->addWidget(controls.slope);
		rowLayout->addWidget(controls.delay);
		rowLayout->addWidget(controls.polarity);
		rowLayout->addStretch(1);

		bassPathForm->addRow(
			fromUtf8(path.id) + tr(" LP:"), rowLayout);

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
	// The skin polishes the fresh rows after this rebuild returns, and the
	// styled fonts can be wider than the construction-time metrics (Minimal's
	// mono face clipped the Invert switches). Measure once more a tick later.
	QTimer::singleShot(0, this, &SubwooferRoutingEditorDialog::updateLeftPaneWidth);
}

void SubwooferRoutingEditorDialog::updateLeftPaneWidth()
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
	leftScroll->setMinimumWidth(contentWidth + barWidth + 12);
}

void SubwooferRoutingEditorDialog::rebuildRoutingViews()
{
	// The plugin's physical layout is the device these views route to: its
	// channels are real, and the bass paths the send view targets are
	// virtual.
	RoutingPortModel bassSendPorts;
	bassSendPorts.fixedSources =
		SubwooferRoutingRoutingAdapter::bassSendSources(model->state());
	bassSendPorts.allowFactors = false;
	bassSendPorts.deviceChannels = physicalTargets(model->state());

	rebuildRoutingView(bassSendRoutingView, bassSendRoutingHint,
		bassSendRoutingLayout,
		SubwooferRoutingRoutingAdapter::toBassSendAssignments(
			model->state()),
		bassPathTargets(model->state()),
		bassSendPorts,
		&SubwooferRoutingEditorDialog::bassSendRoutingEdited);

	RoutingPortModel outputPorts;
	outputPorts.fixedSources =
		SubwooferRoutingRoutingAdapter::outputSources(model->state());
	outputPorts.allowFactors = true;
	outputPorts.deviceChannels = physicalTargets(model->state());

	rebuildRoutingView(outputRoutingView, outputRoutingHint,
		outputRoutingLayout,
		SubwooferRoutingRoutingAdapter::toOutputAssignments(
			model->state()),
		physicalTargets(model->state()),
		outputPorts,
		&SubwooferRoutingEditorDialog::outputRoutingEdited);
}

void SubwooferRoutingEditorDialog::rebuildRoutingView(
	RoutingView*& view,
	QLabel*& hint,
	QVBoxLayout* layout,
	const std::vector<Assignment>& assignments,
	const std::vector<std::wstring>& targets,
	const RoutingPortModel& portModel,
	void (SubwooferRoutingEditorDialog::*editedSlot)())
{
	if (view != nullptr)
	{
		layout->removeWidget(view);
		view->hide();
		view->deleteLater();
		view = nullptr;
	}

	if (hint != nullptr)
	{
		layout->removeWidget(hint);
		hint->hide();
		hint->deleteLater();
		hint = nullptr;
	}

	IRoutingRenderer* renderer =
		SkinManager::instance()->routingRenderer();
	if (renderer == nullptr)
	{
		hint = new QLabel(
			tr("The active heritage skin does not provide a routing editor."),
			layout->parentWidget());
		hint->setWordWrap(true);
		layout->addWidget(hint);
		return;
	}

	view = renderer->create(
		assignments,
		targets,
		portModel,
		layout->parentWidget(),
		SkinManager::instance()->tokens());
	layout->addWidget(view);

	connect(view, &RoutingView::routingChanged,
		this,
		editedSlot);
}

void SubwooferRoutingEditorDialog::refreshValidation()
{
	const subroute::ValidationResult& validation =
		model->validation();

	if (validation.diagnostics.empty())
	{
		validationLabel->setText(tr("State is valid."));
		return;
	}

	const auto error = std::find_if(
		validation.diagnostics.begin(),
		validation.diagnostics.end(),
		[](const subroute::ValidationDiagnostic& diagnostic)
		{
			return diagnostic.severity
				== subroute::DiagnosticSeverity::Error;
		});

	const subroute::ValidationDiagnostic& diagnostic =
		error != validation.diagnostics.end()
			? *error
			: validation.diagnostics.front();

	validationLabel->setText(fromUtf8(diagnostic.message));
}
