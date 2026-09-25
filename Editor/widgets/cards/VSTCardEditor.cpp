/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	Logic ported from Editor/guis/VSTPluginFilterGUI.cpp (Copyright (C) 2017
	Jonas Thedering) into a card-native layout; the plugin session and the
	row document it now shares with that row live in VSTPluginSession and
	VSTRowDocument. store()/parse round-trip verified lossless by
	--selftest-vst. See VSTCardEditor.h for the presentation.
*/

#include "VSTCardEditor.h"
#include "services/registry/RegistryPaths.h"

#include <algorithm>

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include "filters/ConfigPathPolicy.h"
#include "filters/VSTPluginCommand.h"
#include "vst/VST3SpeakerMapping.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/MainWindow.h"
#include "ReferenceCardView.h"
#include "FileReferenceController.h"
#include "VSTBusStrip.h"
#include "VSTSlotFillRail.h"

using std::shared_ptr;
using std::unordered_map;
using std::wstring;

namespace
{
// Display form of the library path: relative to the default VSTPlugins
// directory when it lives beneath it, absolute otherwise.
QString displayPathForLibrary(const wstring& libPath)
{
	QString absolutePath = QString::fromStdWString(libPath);
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;
	return relativePath;
}

QString layoutName(VST3BusLayout layout)
{
	return QString::fromWCharArray(vst3BusLayoutName(layout));
}
}

VSTCardEditor::VSTCardEditor(shared_ptr<VSTPluginLibrary> library, const wstring& chunkData,
	const unordered_map<wstring, float>& paramMap, bool stereoInput,
	const std::optional<VST3BusContract>& busContract,
	std::vector<std::wstring> deviceChannelNames, FilterTable* filterTable, QWidget* parent,
	std::vector<std::wstring> inputChannels, std::vector<std::wstring> outputChannels)
	: IFilterGUI(parent),
	document(busContract, stereoInput, std::move(inputChannels), std::move(outputChannels)),
	session(std::make_unique<VSTPluginSession>(VSTPluginSession::Row::Card, library, chunkData, paramMap)),
	deviceChannelNames(std::move(deviceChannelNames)),
	filterTable(filterTable)
{
	setObjectName(QStringLiteral("VSTCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);

	view = SkinManager::instance()->createReferenceCardView(QStringLiteral("vst"), this);
	// DAW slot grammar: the device identity opens the panel.
	connect(view, SIGNAL(nameActivated()), this, SLOT(openPanel()));
	connect(view, SIGNAL(pathCommitted(QString)), this, SLOT(pathCommitted(QString)));
	root->addWidget(view);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor actionColor(tokens.text);

	selectButton = new QToolButton(view);
	selectButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	selectButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), actionColor, 18));
	connect(selectButton, SIGNAL(clicked()), this, SLOT(selectFile()));
	view->addActionButton(ReferenceCardView::ActionRole::Browse, selectButton);

	// The remedy for a library the audio service cannot read: copy it into
	// the config directory, which the installer ACLs for LOCAL SERVICE and
	// which survives Velopack updates (the install dir's VSTPlugins does
	// not). Shown only when the readability probe fails - unlike the
	// convolution card, a readable plugin is never offered for import,
	// because plugin binaries are machine-installed dependencies
	// (ConfigDependencyScanner states the same rule for bundle imports).
	importButton = new QToolButton(view);
	importButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	importButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/import.svg"), actionColor, 18));
	importButton->setToolTip(tr("Copy the library into the config directory so the audio service can read it"));
	importButton->setVisible(false);
	connect(importButton, SIGNAL(clicked()), this, SLOT(importToConfig()));
	view->addActionButton(ReferenceCardView::ActionRole::Import, importButton);

	openPanelButton = new QPushButton(tr("Open panel"), view);
	openPanelButton->setObjectName(QStringLiteral("VSTCardPanelButton"));
	connect(openPanelButton, SIGNAL(clicked()), this, SLOT(panelButtonClicked()));
	view->addActionButton(ReferenceCardView::ActionRole::OpenPanel, openPanelButton);

	optionsButton = new QToolButton(view);
	optionsButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	optionsButton->setText(QStringLiteral("..."));
	optionsButton->setPopupMode(QToolButton::InstantPopup);
	QMenu* menu = new QMenu(optionsButton);
	menu->setToolTipsVisible(true);
	embedAction = menu->addAction(tr("Embed panel in card"));
	embedAction->setCheckable(true);
	connect(embedAction, SIGNAL(toggled(bool)), this, SLOT(embedToggled(bool)));
	// The repair affordance for saved layout keys: the way to a bare Auto
	// line, and the answer to stale keys on a module that loaded as VST2.
	removeBusAction = menu->addAction(tr("Remove Input/Output layouts"));
	removeBusAction->setToolTip(tr("Deletes the saved VST3 bus layouts from this line."));
	removeBusAction->setEnabled(false);
	connect(removeBusAction, SIGNAL(triggered()), this, SLOT(removeBusLayouts()));
	// The way back from an explicit channel fill to the engine's implicit
	// first-channels default (an identity-looking fill is NOT the same
	// thing: it changes the untargeted channels' passthrough).
	removeFillAction = menu->addAction(tr("Remove channel fill"));
	removeFillAction->setToolTip(tr("Deletes the saved per-slot channel lists from this line."));
	removeFillAction->setEnabled(false);
	connect(removeFillAction, SIGNAL(triggered()), this, SLOT(removeChannelFill()));
	optionsButton->setMenu(menu);
	view->addActionButton(ReferenceCardView::ActionRole::Options, optionsButton);

	// No in-body path pencil: it duplicated the header's raw-line editor and
	// read as "edit this plugin". Browse is the path affordance here.

	// The bus instrument mounts beside the plugin identity (the view decides
	// where exactly); the card is wide, so the contract lives in the row's
	// horizontal slack instead of a stacked extra row.
	busStrip = new VSTBusStrip(view);
	busStrip->setBusLayouts(document.bus().input(), document.bus().output());
	connect(busStrip, &VSTBusStrip::busLayoutsPicked, this, &VSTCardEditor::busLayoutsPicked);
	view->placeBusStrip(busStrip);

	// The channel-fill rails sandwich the reference body inside the card:
	// the input rail under the row header, the output rail under the body.
	// Presence follows the contract (an Auto side has no rail), the fold
	// latch only exists while both rails do.
	inputRail = new VSTSlotFillRail(false, this);
	connect(inputRail, &VSTSlotFillRail::slotPicked, this,
		[this](int slot, const QString& value) { fillSlotPicked(slot, value, false); });
	connect(inputRail, &VSTSlotFillRail::latchToggled, this, &VSTCardEditor::fillLatchToggled);
	root->insertWidget(0, inputRail);
	outputRail = new VSTSlotFillRail(true, this);
	connect(outputRail, &VSTSlotFillRail::slotPicked, this,
		[this](int slot, const QString& value) { fillSlotPicked(slot, value, true); });
	root->insertWidget(root->indexOf(view) + 1, outputRail);
	document.setSelectedChannels(this->deviceChannelNames);
	fillCollapsed = document.fill().inputFill().empty() && document.fill().outputFill().empty();

	frame = new QFrame(this);
	frame->setObjectName(QStringLiteral("VSTCardEmbedFrame"));
	frame->setFrameShape(QFrame::StyledPanel);
	frame->setVisible(false);
	root->addWidget(frame);

	warningTextEdit = new QPlainTextEdit(this);
	warningTextEdit->setObjectName(QStringLiteral("VSTCardWarning"));
	warningTextEdit->setReadOnly(true);
	warningTextEdit->setVisible(false);
	root->addWidget(warningTextEdit);

	reference = new FileReferenceController(
		QStringLiteral("vst"), displayPathForLibrary(library->getLibPath()), this);

	// Let the active skin decorate this VST body (the row is recreated on
	// skin switches, so construction is the only moment needed).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("vst");
	rowInfo.command = QStringLiteral("vstplugin");
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	// The card draws what the session reports; the session never touches
	// the card's widgets beyond the embed host it is handed.
	connect(session.get(), &VSTPluginSession::statusChanged, this, &VSTCardEditor::updateReferenceState);
	connect(session.get(), &VSTPluginSession::stateChanged, this, &VSTCardEditor::pluginStateChanged);
	connect(session.get(), &VSTPluginSession::automated, this, &VSTCardEditor::pluginStateChanged);
	connect(session.get(), &VSTPluginSession::sizeRequested, this, [this](int w, int h) {
		frame->setFixedSize(w, h);
	});

	updateBusControls();
	updateFillRails();
	updateReferenceState();
	updatePermissionWarning();
}

VSTCardEditor::~VSTCardEditor()
{
	if (session->instance() != nullptr)
	{
		if (session->embedded())
			embedToggled(false);
	}
}

void VSTCardEditor::store(QString& command, QString& parameters)
{
	command = "VSTPlugin";

	QString relativePath = reference->writtenPath();

	if (relativePath.contains(" "))
		relativePath = "\"" + relativePath + "\"";
	parameters = "Library " + relativePath;

	// The Library token stays here for its QDir-based path resolution; the
	// body (paired Input/Output, then state) comes from the shared serializer,
	// so this card and the legacy row emit the same grammar. A legacy
	// StereoInput flag left this line as the equivalent Input Stereo / Output
	// Auto contract when the card opened (VSTBusModel); it is never emitted
	// again. The frozen legacy row keeps writing it losslessly.
	VSTPluginCommand cmd;
	cmd.chunkData = session->chunkData();
	cmd.paramMap = session->paramMap();
	cmd.stereoInput = false;
	if (document.bus().contract())
	{
		cmd.busContract = *document.bus().contract();
		cmd.hasBusContract = true;
		cmd.inputChannels = document.fill().inputFill();
		cmd.outputChannels = document.fill().outputFill();
	}
	parameters += QString::fromStdWString(cmd.serialize());
}

void VSTCardEditor::busLayoutsPicked(VST3BusLayout input, VST3BusLayout output)
{
	if (document.bus().contract() && document.bus().input() == input && document.bus().output() == output)
		return;
	document.setLayouts(input, output);
	updateBusControls();
	updateFillRails();
	updateReferenceState();
	updateModel();
}

void VSTCardEditor::removeBusLayouts()
{
	if (!document.bus().contract())
		return;
	document.clearLayouts();
	updateBusControls();
	updateFillRails();
	updateReferenceState();
	updateModel();
}

void VSTCardEditor::fillSlotPicked(int slot, const QString& value, bool output)
{
	document.pickSlot(output, slot, value.toStdWString());
	updateFillRails();
	updateModel();
}

void VSTCardEditor::fillLatchToggled()
{
	fillCollapsed = !fillCollapsed;
	fillCollapsedFromPrefs = true;
	updateFillRails();
}

void VSTCardEditor::removeChannelFill()
{
	if (document.fill().inputFill().empty() && document.fill().outputFill().empty())
		return;
	document.clearFill();
	updateFillRails();
	updateModel();
}

void VSTCardEditor::setChannelFlow(const ChannelFlowAtLine& flow)
{
	document.setSelectedChannels(flow.selected);
	updateFillRails();
}

void VSTCardEditor::updateFillRails()
{
	const VSTSlotFillModel& fillModel = document.fill();

	const bool latchPresent = fillModel.latchPresent();
	// A single rail never folds; the latch quietly disappears with it.
	if (!latchPresent)
		fillCollapsed = false;

	QStringList choices;
	for (const std::wstring& name : fillModel.selectedChannels())
		choices.append(QString::fromStdWString(name));

	for (VSTSlotFillRail* rail : {inputRail, outputRail})
	{
		const bool output = rail == outputRail;
		rail->setChannelChoices(choices);
		QList<VSTSlotFillRail::CellData> cells;
		const int count = fillModel.slotCount(output);
		for (int slot = 0; slot < count; slot++)
		{
			VSTSlotFillRail::CellData cell;
			cell.role = QString::fromStdWString(fillModel.slotRole(output, slot));
			cell.value = QString::fromStdWString(fillModel.slotValue(output, slot));
			cell.silent = fillModel.slotSilent(output, slot);
			cell.defaulted = fillModel.sideDefaulted(output);
			cell.missing = fillModel.slotChannelMissing(output, slot);
			cells.append(cell);
		}
		rail->setCells(cells);
		rail->setCollapsed(fillCollapsed);
	}
	inputRail->setLatchVisible(latchPresent);
	inputRail->setVisible(fillModel.railPresent(false));
	outputRail->setVisible(fillModel.railPresent(true) && !fillCollapsed);

	if (removeFillAction != nullptr)
		removeFillAction->setEnabled(!fillModel.inputFill().empty() || !fillModel.outputFill().empty());
}

void VSTCardEditor::loadPreferences(const QVariantMap& prefs)
{
	session->setAutoApplyDialog(prefs.value("autoApplyDialog", true).toBool());

	if (prefs.contains("slotFillCollapsed"))
	{
		fillCollapsed = prefs.value("slotFillCollapsed").toBool();
		fillCollapsedFromPrefs = true;
		updateFillRails();
	}

	if (prefs.value("embed").toBool())
		embedAction->setChecked(true);   // will also call initPlugin via embedToggled
	else
		session->initPlugin();
	updateBusControls();
	updateReferenceState();
}

void VSTCardEditor::storePreferences(QVariantMap& prefs)
{
	prefs.insert("embed", embedAction->isChecked());
	prefs.insert("autoApplyDialog", session->autoApplyDialog());
	// Only a fold the user actually chose is worth remembering; the default
	// (collapsed while both sides are implicit) re-derives on load.
	if (fillCollapsedFromPrefs)
		prefs.insert("slotFillCollapsed", fillCollapsed);
}

void VSTCardEditor::openPanel()
{
	// The panel is already on screen inside the card; opening the dialog on
	// top would steal the embedded view's window (startEditing recreates the
	// view for the dialog and the card frame would keep showing nothing).
	if (session->embedded())
		return;

	session->initPlugin();
	updateBusControls();
	updateReferenceState();

	session->openDialog(this);
}

void VSTCardEditor::pluginStateChanged()
{
	updateModel();
	updatePermissionWarning();
}

// Map the library / plugin lifecycle onto the reference-card state: the
// loaded plugin's display name first, the file name as the fallback identity,
// the broken library as the missing transition with Locate as recovery.
void VSTCardEditor::updateReferenceState()
{
	const std::shared_ptr<VSTPluginLibrary>& library = session->library();
	const VSTPluginSession::Status& status = session->status();
	reference->setResolvedPath(QString::fromStdWString(library->getLibPath()));
	ReferenceCardState state = reference->describe(tr("No plugin selected"));
	// Plugins routinely live in absolute system paths (Common Files\VST3);
	// the ABS portability hazard badge is noise on a VST slot.
	state.absolutePath = false;
	if (!reference->writtenPath().isEmpty())
	{
		state.missing = state.missing || status.libraryMissing;
		if (session->instance() != nullptr)
		{
			// A .dll can host VST3 and a .vst3 bundle can still load as VST2;
			// the format badge speaks only after the loader established the
			// actual ABI (the extension is not format evidence, issue #216).
			state.formatBadge = library->isVST3()
				? QStringLiteral("VST3") : QStringLiteral("VST2");
			const QString pluginName = QString::fromStdWString(session->instance()->getName());
			if (!pluginName.trimmed().isEmpty())
				state.name = pluginName;
			state.nameClickable = true;
		}
		else if (!state.missing)
		{
			// Library present but not (yet) loaded: clicking the name still
			// attempts the panel, which surfaces the load error honestly.
			state.nameClickable = true;
			if (status.critical && !status.text.isEmpty())
			{
				state.statusText = status.text;
				state.statusSeverity = ReferenceCardState::Severity::Critical;
			}
		}
	}

	// The audio service opens the library with LOCAL SERVICE's rights, not
	// the user's: a file under the user profile loads fine in the Editor and
	// still never loads during playback. The probe deliberately does not
	// require a loaded plugin instance - gated on one, the verdict appeared
	// when a panel opened and silently vanished on the next row rebuild,
	// which read as a phantom error.
	// The engine's location rule is judged too: a plug-in on a share loads
	// here and is refused there.
	bool offerImport = false;
	const QString problem = reference->writtenPath().isEmpty() || state.missing ? QString()
		: FileReferenceController::audioServiceProblem(QString::fromStdWString(library->getLibPath()),
			filterTable != nullptr ? filterTable->getConfigPath() : QString());
	if (!problem.isEmpty())
	{
		if (state.statusText.isEmpty())
		{
			state.statusText = problem;
			state.statusSeverity = ReferenceCardState::Severity::Critical;
		}
		offerImport = filterTable != nullptr;
	}
	importButton->setVisible(offerImport);

	// The bus contract's long-form message rides the card's status line;
	// a load error already occupying it stays the more urgent fact.
	if (state.statusText.isEmpty() && !busStatusText.isEmpty())
	{
		state.statusText = busStatusText;
		state.statusSeverity = busStatusSeverity;
	}

	const bool locate = state.missing && !reference->writtenPath().isEmpty();
	selectButton->setText(locate ? tr("Locate...") : QString());
	selectButton->setToolTip(locate ? tr("Locate the missing plugin library") : tr("Select VST plugin"));
	openPanelButton->setEnabled(!state.missing && !reference->writtenPath().isEmpty());

	// Post the loaded ABI for the row chrome (CommandRowFrame samples the
	// property at paint time): rack engraves it into the brass nameplate.
	// The editor is parented into the row after construction, so the walk
	// only finds the frame from the loadPreferences pass onward.
	for (QWidget* ancestor = parentWidget(); ancestor != nullptr; ancestor = ancestor->parentWidget())
	{
		if (ancestor->objectName() == QLatin1String("FilterCardRow"))
		{
			if (ancestor->property("rowFormatTag").toString() != state.formatBadge)
			{
				ancestor->setProperty("rowFormatTag", state.formatBadge);
				ancestor->update();
			}
			break;
		}
	}

	view->setState(state);
}

// The bus instrument's semantics in one place: when the strip shows, when
// its selectors may act, what the compact verdict says, and which long-form
// message the card's status line carries. Call before updateReferenceState -
// the status line and the strip's visibility feed the view's state pass.
void VSTCardEditor::updateBusControls()
{
	const VSTBusModel& busModel = document.bus();
	VSTPluginInstance* effect = session->instance();
	const std::shared_ptr<VSTPluginLibrary>& library = session->library();
	const bool embedded = session->embedded();

	busStatusText.clear();
	busStatusSeverity = ReferenceCardState::Severity::None;
	removeBusAction->setEnabled(busModel.contract().has_value());
	busStrip->setBusLayouts(busModel.input(), busModel.output());

	// No loaded plugin and no saved contract: nothing to show or protect.
	// A saved contract stays visible (though locked) even while the library
	// is missing, so reopening a config never hides data it still carries.
	const bool showStrip = effect != nullptr || busModel.contract().has_value();
	busStrip->setVisible(showStrip);
	if (!showStrip)
		return;

	if (effect == nullptr)
	{
		busStrip->setSelectorsEnabled(false, tr("The bus layout can be changed after the plugin loads."));
		busStrip->setVerdict(QString(), VstBusFrameState::Tone::Neutral);
		return;
	}

	if (!library->isVST3())
	{
		// The loaded ABI decides: VST2 ignores the layout keys (issue #216).
		busStrip->setSelectorsEnabled(false, tr("Input and Output layouts are only supported for VST3 plugins."));
		if (busModel.contract())
		{
			busStrip->setVerdict(QStringLiteral("VST2"), VstBusFrameState::Tone::Warning);
			busStatusText = tr("This module loaded as VST2 and ignores the saved Input/Output layouts. Remove them via Options.");
			busStatusSeverity = ReferenceCardState::Severity::Warning;
		}
		else
		{
			busStrip->setVerdict(QStringLiteral("VST2"), VstBusFrameState::Tone::Neutral);
		}
		return;
	}

	if (embedded)
	{
		// Renegotiating would tear the processor state out from under the
		// open panel; the last verdict stays on display.
		busStrip->setSelectorsEnabled(false, tr("Close the plugin panel to change the bus layout."));
		return;
	}

	busStrip->setSelectorsEnabled(true);

	// Probe the negotiation on the editor's own instance - the same call the
	// engine makes, so the verdict states what playback will actually do.
	const VST3BusLayout requestedInput = busModel.input();
	const VST3BusLayout requestedOutput = busModel.output();
	const std::vector<std::wstring> inputHints = vst3speakers::channelNamesForLayout(
		requestedInput, deviceChannelNames);
	const std::vector<std::wstring> outputHints = vst3speakers::channelNamesForLayout(
		requestedOutput, deviceChannelNames);
	const int automaticChannelCount = !deviceChannelNames.empty()
		? static_cast<int>(deviceChannelNames.size())
		: std::max({2, effect->numInputs(), effect->numOutputs()});
	const bool accepted = effect->negotiateBusLayouts(requestedInput, requestedOutput,
		automaticChannelCount, inputHints, outputHints);

	if (!accepted)
	{
		// Lamp-only, like the accepted verdict: a danger lamp beside the
		// selectors says it, and the sentence belongs to the status line.
		// A "Rejected" word in the strip restated the lamp (maintainer
		// judgement, r2: 사족).
		busStrip->setVerdict(QString(), VstBusFrameState::Tone::Critical);
		// One short sentence: the fact and its consequence. The full escape
		// routes (change or remove the layouts) are the selectors sitting
		// right there - prose walking through them read as a wall (r3).
		busStatusText = tr("The plugin rejected %1 in / %2 out. Audio passes through unchanged.")
			.arg(layoutName(requestedInput), layoutName(requestedOutput));
		busStatusSeverity = ReferenceCardState::Severity::Critical;
		return;
	}

	// An accepted all-explicit contract needs no words - the selectors print
	// the pair, the lamp says it engaged. Any Auto direction makes the
	// negotiated result the informative fact, so the verdict prints what
	// actually engaged; an arrangement outside the config grammar honestly
	// reads as a channel count.
	const bool anyAuto = requestedInput == VST3BusLayout::Auto
		|| requestedOutput == VST3BusLayout::Auto;
	if (anyAuto)
	{
		const std::optional<VST3BusLayout> activeInput = effect->getNegotiatedVST3InputLayout();
		const std::optional<VST3BusLayout> activeOutput = effect->getNegotiatedVST3OutputLayout();
		const QString inputText = activeInput
			? layoutName(*activeInput) : tr("%1 ch").arg(effect->numInputs());
		const QString outputText = activeOutput
			? layoutName(*activeOutput) : tr("%1 ch").arg(effect->numOutputs());
		busStrip->setVerdictPair(inputText, outputText,
			busModel.contract() ? VstBusFrameState::Tone::Success : VstBusFrameState::Tone::Neutral);
	}
	else
	{
		busStrip->setVerdict(QString(), VstBusFrameState::Tone::Success);
	}
	if (busModel.migratedLegacyStereoInput())
		busStatusText = tr("The legacy Stereo input option now reads as Input Stereo, Output Auto and will be saved that way.");
}

void VSTCardEditor::pathCommitted(const QString& text)
{
	reference->setWrittenPath(text);
	if (session->libraryDiffers(text))
	{
		if (session->instance() != nullptr && embedAction->isChecked())
			embedToggled(false);
		session->replaceLibrary(text);

		updateModel();
		updatePermissionWarning();

		if (embedAction->isChecked())
			embedToggled(true);
	}
	updateBusControls();
	updateReferenceState();
}

void VSTCardEditor::selectFile()
{
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QString lastDir = settings.value("vst/lastDir", "").toString();
	if (lastDir == "")
		lastDir = pluginsDir.absolutePath();

	QFileInfo fileInfo(lastDir);
	if (!reference->writtenPath().isEmpty())
		fileInfo.setFile(pluginsDir, reference->writtenPath());

	const QString absolutePath = reference->chooseExistingFile(
		this, tr("Select VST plugin"), fileInfo.absoluteFilePath(),
		tr("VST plugins (*.dll *.vst3)"), pluginsDir.absolutePath(),
		reference->writtenPath().isEmpty() ? QString() : fileInfo.fileName(),
		/*selectVst3Bundles=*/ true);
	if (!absolutePath.isEmpty())
	{
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		pathCommitted(reference->writtenPath());
	}
}

void VSTCardEditor::importToConfig()
{
	if (filterTable == nullptr)
		return;

	reference->setResolvedPath(QString::fromStdWString(session->library()->getLibPath()));
	if (!reference->importIntoConfig(this, filterTable->getConfigPath()))
		return;

	// The engine resolves relative Library references against the install
	// VSTPlugins directory, not the config directory, so the imported copy
	// has to be written by its absolute path. pathCommitted reloads the
	// library from the copy and re-runs the readability verdict.
	pathCommitted(QDir::toNativeSeparators(reference->resolvedPath()));
}

void VSTCardEditor::panelButtonClicked()
{
	// One visible button owns the panel either way: it opens the dialog while
	// nothing is embedded, and closes the embedded panel otherwise. The
	// options-menu checkbox stays in sync because closing goes through it.
	if (embedAction->isChecked())
		embedAction->setChecked(false);
	else
		openPanel();
}

void VSTCardEditor::embedToggled(bool checked)
{
	session->initPlugin();
	updateReferenceState();

	const bool enable = checked && session->instance() != nullptr;
	if (enable != session->embedded())
	{
		// The host is shown before the session embeds into it, and hidden
		// again when embedding failed (reported through the status).
		frame->setVisible(enable);
		if (!session->setEmbedded(enable, frame))
			frame->setVisible(false);
	}

	// A checked action without a live embed (plugin missing or crashed while
	// opening the panel) would leave the card claiming a panel it does not
	// show; drop the check so the button reads "Open panel" again. The
	// recursive toggle is a no-op: embedded already matches.
	if (checked && !session->embedded() && embedAction->isChecked())
		embedAction->setChecked(false);

	// The button stays visible while embedded - it is the way out. Hiding it
	// left the embed removable only through the options menu, which read as
	// "the panel cannot be closed".
	openPanelButton->setText(session->embedded() ? tr("Close panel") : tr("Open panel"));

	// The strip locks while the panel is embedded and unlocks with it.
	updateBusControls();
	updateReferenceState();
}

// The chunk-referenced-files warning. The library's own readability verdict
// lives on the reference card's status line (updateReferenceState), where it
// is computed from the path alone; this one scans the saved plugin state and
// so needs only the chunk data, never a loaded plugin instance
// (VSTPluginSession::chunkPermissionWarning).
void VSTCardEditor::updatePermissionWarning()
{
	const QString text = session->chunkPermissionWarning();

	if (text.isEmpty())
	{
		warningTextEdit->setVisible(false);
		warningTextEdit->setPlainText("");
	}
	else
	{
		warningTextEdit->setPlainText(text);
		QSize textSize = warningTextEdit->fontMetrics().size(0, text);
		warningTextEdit->setFixedSize(textSize + QSize(40, 15));
		warningTextEdit->setVisible(true);
	}
}

#include <unordered_map>
#include <vector>

#include "FilterCardEditorRegistry.h"
#include "vst/VSTPluginLibrary.h"

REGISTER_FILTER_CARD_EDITOR(VSTPlugin, [](FilterTable* filterTable, const QString&, const QString& parameters) -> IFilterGUI* {
	// Parse straight into the shared command struct, like the legacy row's
	// factory: the engine's exact grammar without building (and destroying)
	// a VSTPluginFilter, and no plugin binary is loaded here - getInstance
	// only returns the cached library object. A line the engine's factory
	// would refuse (a malformed contract, no library, a library path the
	// config path policy does not open) still opens an empty card, as it did
	// when this path went through the factory. The store()/parse round-trip
	// is verified lossless (--selftest-vst).
	const VSTPluginCommand cmd = VSTPluginCommand::parse(L"", parameters.toStdWString());
	std::wstring refusal;
	const bool usable = cmd.valid && !cmd.libraryPath.empty()
		&& ConfigPathPolicy::allowsOpen(cmd.libraryPath, L"", refusal);
	VSTCardEditor* editor;
	if (usable)
	{
		// The factory keeps the contract for parser-only callers and drops
		// the StereoInput flag with it (the parser rejects the two together).
		const std::optional<VST3BusContract> busContract = cmd.hasBusContract
			? std::optional<VST3BusContract>(cmd.busContract) : std::nullopt;
		editor = new VSTCardEditor(VSTPluginLibrary::getInstance(cmd.libraryPath), cmd.chunkData, cmd.paramMap,
			cmd.hasBusContract ? false : cmd.stereoInput, busContract,
			filterTable != nullptr ? filterTable->getChannelNames() : std::vector<std::wstring>(),
			filterTable, nullptr,
			cmd.hasBusContract ? cmd.inputChannels : std::vector<std::wstring>(),
			cmd.hasBusContract ? cmd.outputChannels : std::vector<std::wstring>());
	}
	else
	{
		editor = new VSTCardEditor(VSTPluginLibrary::getInstance(L""), L"", std::unordered_map<std::wstring, float>(),
			false, std::nullopt, std::vector<std::wstring>(), filterTable);
	}
	return editor;
})
