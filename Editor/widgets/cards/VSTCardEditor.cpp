/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Logic ported from Editor/guis/VSTPluginFilterGUI.cpp (Copyright (C) 2017
	Jonas Thedering) into a card-native layout; store()/parse round-trip verified
	lossless by --selftest-vst. See VSTCardEditor.h for the presentation.
*/

#include "VSTCardEditor.h"
#include "services/registry/RegistryPaths.h"

#include <algorithm>

#include <QAbstractEventDispatcher>
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "services/security/AudioEngineAccess.h"
#include "filters/VSTPluginCommand.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/helpers/VstChunkScan.h"
#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/MainWindow.h"
#include "Editor/guis/VSTPluginFilterGUIDialog.h"
#include "ReferenceCardView.h"
#include "FileReferenceController.h"
#include "VSTBusLayoutControls.h"

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

QString layoutPair(VST3BusLayout input, VST3BusLayout output)
{
	return QStringLiteral("%1 \u2192 %2").arg(layoutName(input), layoutName(output));
}

QString activeBusDescription(const VSTPluginInstance* effect)
{
	const std::optional<VST3BusLayout> inputLayout = effect->getNegotiatedVST3InputLayout();
	const std::optional<VST3BusLayout> outputLayout = effect->getNegotiatedVST3OutputLayout();
	const QString input = inputLayout ? layoutName(*inputLayout)
		: QStringLiteral("%1 ch").arg(effect->numInputs());
	const QString output = outputLayout ? layoutName(*outputLayout)
		: QStringLiteral("%1 ch").arg(effect->numOutputs());
	return QStringLiteral("%1 \u2192 %2 (%3 in / %4 out)")
		.arg(input, output)
		.arg(effect->numInputs())
		.arg(effect->numOutputs());
}
}

VSTCardEditor::VSTCardEditor(shared_ptr<VSTPluginLibrary> library, const wstring& chunkData,
	const unordered_map<wstring, float>& paramMap, bool stereoInput,
	const std::optional<VST3BusContract>& busContract, std::vector<std::wstring> deviceChannelNames,
	QWidget* parent)
	: IFilterGUI(parent), library(library), chunkData(chunkData), paramMap(paramMap),
	busLayoutModel(busContract, stereoInput), deviceChannelNames(std::move(deviceChannelNames))
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
	optionsButton->setMenu(menu);
	view->addActionButton(ReferenceCardView::ActionRole::Options, optionsButton);

	editButton = new QToolButton(view);
	editButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	editButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/pencil.svg"), actionColor, 18));
	editButton->setToolTip(tr("Edit the path as text"));
	connect(editButton, &QToolButton::clicked, view, &ReferenceCardView::enterEditMode);
	view->addActionButton(ReferenceCardView::ActionRole::EditPath, editButton);

	busControls = new VSTBusLayoutControls(this);
	busControls->setLayouts(busLayoutModel.input(), busLayoutModel.output());
	connect(busControls, SIGNAL(layoutsEdited(VST3BusLayout,VST3BusLayout)),
		this, SLOT(busLayoutsEdited(VST3BusLayout,VST3BusLayout)));
	connect(busControls, SIGNAL(removeLayoutsRequested()), this, SLOT(removeBusLayouts()));
	root->addWidget(busControls);

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

	updateReferenceState();
	updateBusControls();
	updatePermissionWarning();
}

VSTCardEditor::~VSTCardEditor()
{
	if (effect != nullptr)
	{
		if (embedded)
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
	// body (paired Input/Output, then state) comes from the shared serializer.
	// ModernCards migrates legacy StereoInput to Input Stereo / Output Auto and
	// never emits the obsolete flag again; the frozen legacy row remains lossless.
	VSTPluginCommand cmd;
	cmd.chunkData = chunkData;
	cmd.paramMap = paramMap;
	cmd.stereoInput = false;
	if (busLayoutModel.contract())
	{
		cmd.busContract = *busLayoutModel.contract();
		cmd.hasBusContract = true;
	}
	parameters += QString::fromStdWString(cmd.serialize());
}

void VSTCardEditor::busLayoutsEdited(VST3BusLayout input, VST3BusLayout output)
{
	if (busLayoutModel.contract()
		&& busLayoutModel.input() == input && busLayoutModel.output() == output)
		return;
	busLayoutModel.setLayouts(input, output);
	updateBusControls();
	updateModel();
}

void VSTCardEditor::removeBusLayouts()
{
	if (!busLayoutModel.contract())
		return;
	busLayoutModel.removeLayouts();
	busControls->setLayouts(VST3BusLayout::Auto, VST3BusLayout::Auto);
	updateBusControls();
	updateModel();
}

void VSTCardEditor::loadPreferences(const QVariantMap& prefs)
{
	autoApplyDialog = prefs.value("autoApplyDialog").toBool();

	if (prefs.value("embed").toBool())
		embedAction->setChecked(true);   // will also call initPlugin via embedToggled
	else
		initPlugin();
	updateReferenceState();
}

void VSTCardEditor::storePreferences(QVariantMap& prefs)
{
	prefs.insert("embed", embedAction->isChecked());
	prefs.insert("autoApplyDialog", autoApplyDialog);
}

void VSTCardEditor::openPanel()
{
	// The panel is already on screen inside the card; opening the dialog on
	// top would steal the embedded view's window (startEditing recreates the
	// view for the dialog and the card frame would keep showing nothing).
	if (embedded)
		return;

	initPlugin();
	updateReferenceState();

	if (effect != nullptr)
	{
		effect->writeToEffect(chunkData, paramMap);

		VSTPluginFilterGUIDialog dialog(this, effect.get(), autoApplyDialog);
		connect(dialog.getApplyButton(), SIGNAL(pressed()), SLOT(applyDialog()));
		connect(dialog.getAutoApplyCheckBox(), SIGNAL(toggled(bool)), SLOT(autoApplyToggled(bool)));
		connect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), SLOT(onIdle()));

		if (dialog.exec() == QDialog::Accepted)
		{
			effect->readFromEffect(chunkData, paramMap);
			updateModel();
			updatePermissionWarning();
		}
		disconnect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), this, SLOT(onIdle()));
	}
}

void VSTCardEditor::applyDialog()
{
	effect->readFromEffect(chunkData, paramMap);
	updateModel();
	updatePermissionWarning();
}

void VSTCardEditor::autoApplyToggled(bool checked)
{
	autoApplyDialog = checked;
}

void VSTCardEditor::initPlugin()
{
	if (effect != nullptr)
	{
		updateBusControls();
		return;
	}

	initErrorText.clear();
	libraryMissing = false;

	if (library->getLibPath() == L"")
	{
		libraryMissing = true;
	}
	else
	{
		int result = library->initialize();
		if (result < 0)
		{
			switch (result)
			{
			case AbstractLibrary::FILE_NOT_FOUND:
				libraryMissing = true;
				break;
			case AbstractLibrary::LOADING_FAILED:
				initErrorText = tr("Library could not be loaded.");
				break;
			case AbstractLibrary::FUNCTIONS_MISSING:
				initErrorText = tr("Library does not contain needed functions.");
				break;
			case AbstractLibrary::WRONG_ARCHITECTURE:
#ifdef _WIN64
				int bitDepth = 64;
#else
				int bitDepth = 32;
#endif
				initErrorText = tr("Library has the wrong architecture. Only %1-bit libraries are supported.").arg(bitDepth);
				break;
			}
		}
		else
		{
			effect = std::make_unique<VSTPluginInstance>(library, 1);
			if (effect->initialize())
			{
				effect->setLanguage(QLocale().language() == QLocale::German ? 2 : 1);
				effect->setAutomateFunc([this]() { onAutomate(); });
			}
			else
			{
				effect.reset();
				initErrorText = tr("Plugin crashed during initialization.");
			}
		}
	}

	updateBusControls();
}

// Map the library / plugin lifecycle onto the reference-card state: the
// loaded plugin's display name first, the file name as the fallback identity,
// the broken library as the missing transition with Locate as recovery.
void VSTCardEditor::updateReferenceState()
{
	reference->setResolvedPath(QString::fromStdWString(library->getLibPath()));
	ReferenceCardState state = reference->describe(tr("No plugin selected"));
	if (!reference->writtenPath().isEmpty())
	{
		state.missing = state.missing || libraryMissing;
		if (effect != nullptr)
		{
			// A .dll can host VST3 and a bundle can still fail its ABI probe.
			// Advertise the format only after the loader has established it.
			state.formatBadge = library->isVST3() ? QStringLiteral("VST3") : QStringLiteral("VST2");
			const QString pluginName = QString::fromStdWString(effect->getName());
			if (!pluginName.trimmed().isEmpty())
				state.name = pluginName;
			state.nameClickable = true;
		}
		else if (!state.missing)
		{
			// Library present but not (yet) loaded: clicking the name still
			// attempts the panel, which surfaces the load error honestly.
			state.nameClickable = true;
			if (!initErrorText.isEmpty())
			{
				state.statusText = initErrorText;
				state.statusSeverity = ReferenceCardState::Severity::Critical;
			}
		}
	}

	const bool locate = state.missing && !reference->writtenPath().isEmpty();
	selectButton->setText(locate ? tr("Locate...") : QString());
	selectButton->setToolTip(locate ? tr("Locate the missing plugin library") : tr("Select VST plugin"));
	openPanelButton->setEnabled(!state.missing && !reference->writtenPath().isEmpty());

	view->setState(state);
}

void VSTCardEditor::updateBusControls()
{
	busControls->setLayouts(busLayoutModel.input(), busLayoutModel.output());

	if (effect == nullptr)
	{
		const QString reason = tr("Bus controls become available after the plug-in loads.");
		busControls->setControlsEnabled(false, reason);
		busControls->setStatus(reason, VSTBusLayoutControls::StatusTone::Neutral);
		return;
	}

	if (!library->isVST3())
	{
		const QString reason = tr("Input and Output layouts are available only for VST3 plug-ins.");
		busControls->setControlsEnabled(false, reason);
		if (busLayoutModel.contract())
		{
			busControls->setStatus(
				tr("VST2 ignores the saved %1 bus layouts. Remove them to avoid misleading settings.")
					.arg(layoutPair(busLayoutModel.input(), busLayoutModel.output())),
				VSTBusLayoutControls::StatusTone::Warning, true);
		}
		else
		{
			busControls->setStatus(
				tr("VST2 fixed bus: %1 in \u2192 %2 out. No layout keys are saved.")
					.arg(effect->numInputs()).arg(effect->numOutputs()),
				VSTBusLayoutControls::StatusTone::Neutral);
		}
		return;
	}

	if (embedded)
	{
		const QString reason = tr("Close the embedded panel before changing the VST3 bus layout.");
		busControls->setControlsEnabled(false, reason);
		busControls->setStatus(
			tr("Bus controls are locked while the panel is open. Current bus: %1.")
				.arg(activeBusDescription(effect.get())),
			VSTBusLayoutControls::StatusTone::Neutral);
		return;
	}

	busControls->setControlsEnabled(true);
	if (!busLayoutModel.contract())
	{
		busControls->setStatus(
			tr("Detected bus: %1. Choose Input or Output to save an explicit contract.")
				.arg(activeBusDescription(effect.get())),
			VSTBusLayoutControls::StatusTone::Neutral);
		return;
	}

	const VST3BusLayout requestedInput = busLayoutModel.input();
	const VST3BusLayout requestedOutput = busLayoutModel.output();
	const std::vector<std::wstring> inputHints = requestedInput == VST3BusLayout::Auto
		? deviceChannelNames : vst3BusLayoutChannelNames(requestedInput);
	const std::vector<std::wstring> outputHints = requestedOutput == VST3BusLayout::Auto
		? deviceChannelNames : vst3BusLayoutChannelNames(requestedOutput);
	effect->setBusChannelNameHints(inputHints, outputHints);

	const int automaticChannelCount = !deviceChannelNames.empty()
		? static_cast<int>(deviceChannelNames.size())
		: std::max({ 2, effect->numInputs(), effect->numOutputs() });
	const bool accepted = effect->negotiateBusLayouts(
		requestedInput, requestedOutput, automaticChannelCount);
	const QString requested = layoutPair(requestedInput, requestedOutput);
	if (accepted)
	{
		QString text = tr("Accepted bus: %1. Active bus: %2.")
			.arg(requested, activeBusDescription(effect.get()));
		if (busLayoutModel.migratedLegacyStereoInput())
			text += tr(" Legacy StereoInput was migrated to Input Stereo / Output Auto.");
		busControls->setStatus(text, VSTBusLayoutControls::StatusTone::Success);
	}
	else
	{
		busControls->setStatus(
			tr("Rejected bus: %1. Audio will pass through until the layout is changed or removed.")
				.arg(requested),
			VSTBusLayoutControls::StatusTone::Critical);
	}
}

void VSTCardEditor::pathCommitted(const QString& text)
{
	reference->setWrittenPath(text);
	if (QString::fromStdWString(library->getLibPath()) != text)
	{
		int oldId = 0;
		if (effect != nullptr)
		{
			oldId = effect->uniqueID();
			if (embedAction->isChecked())
				embedToggled(false);
			effect.reset();
		}

		QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
		QString path = text;
		if (path.length() > 0)
			path = QDir::toNativeSeparators(QFileInfo(pluginsDir, text).absoluteFilePath());
		library = VSTPluginLibrary::getInstance(path.toStdWString());
		initPlugin();

		if (effect == nullptr || oldId == 0 || effect->uniqueID() != oldId)
		{
			chunkData = L"";
			paramMap.clear();
		}

		updateModel();
		updatePermissionWarning();

		if (embedAction->isChecked())
			embedToggled(true);
	}
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
		reference->writtenPath().isEmpty() ? QString() : fileInfo.fileName());
	if (!absolutePath.isEmpty())
	{
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		pathCommitted(reference->writtenPath());
	}
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
	initPlugin();
	updateReferenceState();

	bool enable = checked;
	if (effect == nullptr)
		enable = false;

	if (enable != embedded)
	{
		embedded = enable;
		frame->setVisible(enable);

		if (enable)
		{
			if (embedPlugin())
			{
				effect->setSizeWindowFunc([this](int w, int h) { onSizeWindow(w, h); });
				connect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), SLOT(onIdle()));
			}
			else
			{
				embedded = false;
				frame->setVisible(false);

				initErrorText = tr("Plugin crashed when opening panel.");
				updateReferenceState();
			}
		}
		else
		{
			if (effect != nullptr)
			{
				effect->stopEditing();
				effect->setSizeWindowFunc(nullptr);
			}
			disconnect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), this, SLOT(onIdle()));
		}
	}

	// A checked action without a live embed (plugin missing or crashed while
	// opening the panel) would leave the card claiming a panel it does not
	// show; drop the check so the button reads "Open panel" again. The
	// recursive toggle is a no-op: embedded already matches.
	if (checked && !embedded && embedAction->isChecked())
		embedAction->setChecked(false);

	// The button stays visible while embedded - it is the way out. Hiding it
	// left the embed removable only through the options menu, which read as
	// "the panel cannot be closed".
	openPanelButton->setText(embedded ? tr("Close panel") : tr("Open panel"));
	updateBusControls();
}

void VSTCardEditor::onIdle()
{
	if (effect != nullptr)
	{
		effect->doIdle();

		if (embedded || autoApplyDialog)
		{
			if (!lastReadTimer.isValid() || lastReadTimer.elapsed() > 1000)
			{
				wstring newChunkData;
				unordered_map<wstring, float> newParamMap;
				effect->readFromEffect(newChunkData, newParamMap);
				if (newChunkData != chunkData || newParamMap != paramMap)
				{
					chunkData = newChunkData;
					paramMap = newParamMap;
					updateModel();
					updatePermissionWarning();
				}
				lastReadTimer.restart();
			}
		}
	}
}

void VSTCardEditor::onAutomate()
{
	if (embedded || autoApplyDialog)
	{
		effect->readFromEffect(chunkData, paramMap);
		updateModel();
		updatePermissionWarning();
	}
}

void VSTCardEditor::onSizeWindow(int w, int h)
{
	if (embedded)
		frame->setFixedSize(w, h);
}

bool VSTCardEditor::embedPlugin()
{
	bool result = true;
	__try
	{
		effect->writeToEffect(chunkData, paramMap);

		HWND hwnd = (HWND)frame->winId();
		short width, height;
		effect->startEditing(hwnd, &width, &height, frame->devicePixelRatioF());
		frame->setFixedSize(width, height);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		result = false;
	}
	return result;
}

void VSTCardEditor::updatePermissionWarning()
{
	if (effect == nullptr)
	{
		warningTextEdit->setVisible(false);
		return;
	}

	if (!FileReferenceController::isReadableByAudioService(
		QString::fromStdWString(library->getLibPath())))
	{
		QString text = tr("The library is not readable by the audio service.\nChange the file permissions or copy the file to the VSTPlugins directory.");
		warningTextEdit->setPlainText(text);
		QSize textSize = warningTextEdit->fontMetrics().size(0, text);
		warningTextEdit->setFixedSize(textSize + GUIHelper::scale(QSize(40, 15)));
		warningTextEdit->setVisible(true);
		return;
	}

	const QStringList files = vstChunkUnreadablePaths(chunkData);

	if (files.isEmpty())
	{
		warningTextEdit->setVisible(false);
		warningTextEdit->setPlainText("");
	}
	else
	{
		QString text = tr("The plugin seemingly accesses these files not readable by the audio service:\n"
				"%0\n"
				"Change the file permissions or copy the files to the config directory.").arg(files.join("\n"));
		warningTextEdit->setPlainText(text);
		QSize textSize = warningTextEdit->fontMetrics().size(0, text);
		warningTextEdit->setFixedSize(textSize + GUIHelper::scale(QSize(40, 15)));
		warningTextEdit->setVisible(true);
	}
}

#include <unordered_map>
#include <vector>

#include "FilterCardEditorRegistry.h"
#include "filters/VSTPluginFilter.h"
#include "filters/VSTPluginFilterFactory.h"
#include "vst/VSTPluginInstance.h"
#include "vst/VSTPluginLibrary.h"

REGISTER_FILTER_CARD_EDITOR(VSTPlugin, [](FilterTable* filterTable, const QString&, const QString& parameters) -> IFilterGUI* {
	// Parse the line into the engine's VST filter (no plugin DLL is loaded
	// for configPath == L""), then hand the opaque state to the card editor.
	// The store()/parse round-trip is verified lossless (--selftest-vst).
	VSTPluginFilterFactory factory;
	std::wstring commandWStr = L"VSTPlugin";
	std::wstring paramWStr = parameters.toStdWString();
	FilterVector filters = factory.createFilter(L"", commandWStr, paramWStr);
	VSTCardEditor* editor;
	if (!filters.empty())
	{
		VSTPluginFilter* filter = static_cast<VSTPluginFilter*>(filters[0].get());
		editor = new VSTCardEditor(filter->getLibrary(), filter->getChunkData(), filter->getParamMap(),
			filter->getStereoInput(), filter->getBusContract(),
			filterTable != nullptr ? filterTable->getChannelNames() : std::vector<std::wstring>());
	}
	else
	{
		editor = new VSTCardEditor(VSTPluginLibrary::getInstance(L""), L"", std::unordered_map<std::wstring, float>());
	}
	return editor;
})
