/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2017  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <QFileInfo>
#include "services/registry/RegistryPaths.h"
#include "Editor/widgets/cards/FileReferenceController.h"
#include <QFileDialog>
#include <QSettings>
#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStringList>

#include "filters/VSTPluginCommand.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/MainWindow.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "VSTPluginFilterGUI.h"
#include "ui_VSTPluginFilterGUI.h"

using std::unordered_map;
using std::wstring;

VSTPluginFilterGUI::VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap,
	bool stereoInput, const std::optional<VST3BusContract>& busContract,
	std::vector<std::wstring> inputChannels, std::vector<std::wstring> outputChannels)
	: ui(std::make_unique<Ui::VSTPluginFilterGUI>()), stereoInput(stereoInput),
	document(busContract, false, std::move(inputChannels), std::move(outputChannels)),
	session(std::make_unique<VSTPluginSession>(VSTPluginSession::Row::Legacy, library, chunkData, paramMap))
{
	ui->setupUi(this);
	ui->frame->setVisible(false);
	updatePermissionWarning();

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;
	ui->pathLineEdit->setText(relativePath);

	QMenu* menu = new QMenu(ui->optionsButton);
	menu->setToolTipsVisible(true);
	menu->addAction(ui->embedAction);
	// The stereo-input toggle lives in the shared Options menu on purpose: the
	// interaction layer is skin-independent, so no per-skin chrome answer is
	// needed and the legacy row and the card behave identically.
	stereoInputAction = new QAction(tr("Stereo input"), this);
	stereoInputAction->setCheckable(true);
	stereoInputAction->setChecked(stereoInput);
	stereoInputAction->setToolTip(tr("Use for upmixers that expand a stereo signal to multichannel."));
	connect(stereoInputAction, &QAction::toggled, this, &VSTPluginFilterGUI::stereoInputToggled);
	menu->addAction(stereoInputAction);
	ui->optionsButton->setMenu(menu);

	// The VST3 main-bus contract as two plain dropdowns. The layout names are
	// config tokens, not prose, so they stay untranslated.
	for (const VST3BusLayoutDefinition& definition : vst3BusLayoutTable)
	{
		const QString name = QString::fromWCharArray(definition.name);
		ui->busInputComboBox->addItem(name, static_cast<int>(definition.layout));
		ui->busOutputComboBox->addItem(name, static_cast<int>(definition.layout));
	}
	connect(ui->busInputComboBox, &QComboBox::activated, this, &VSTPluginFilterGUI::busLayoutPicked);
	connect(ui->busOutputComboBox, &QComboBox::activated, this, &VSTPluginFilterGUI::busLayoutPicked);

	// The channel-fill rows sit between the bus dropdowns and the embed
	// frame, inside the row (never below the table's add button). Their
	// combos are rebuilt from the document's fill whenever the contract, the
	// lists or the selected channels change.
	QGridLayout* grid = static_cast<QGridLayout*>(layout());
	inputFillRow = new QWidget(this);
	new QHBoxLayout(inputFillRow);
	static_cast<QHBoxLayout*>(inputFillRow->layout())->setContentsMargins(0, 0, 0, 0);
	grid->addWidget(inputFillRow, 3, 0, 1, 4);
	outputFillRow = new QWidget(this);
	new QHBoxLayout(outputFillRow);
	static_cast<QHBoxLayout*>(outputFillRow->layout())->setContentsMargins(0, 0, 0, 0);
	grid->addWidget(outputFillRow, 4, 0, 1, 4);
	fillCollapsed = document.fill().inputFill().empty() && document.fill().outputFill().empty();

	updateBusControls();
	updateFillRows();

	// Legacy row: it stays functional under every skin, so it consults the
	// same chrome hook as the card editors (legacyRow marks it for skins
	// that want to leave the legacy path untouched).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("vst");
	rowInfo.command = QStringLiteral("vstplugin");
	rowInfo.legacyRow = true;
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	connect(session.get(), &VSTPluginSession::statusChanged, this, &VSTPluginFilterGUI::showStatus);
	connect(session.get(), &VSTPluginSession::stateChanged, this, &VSTPluginFilterGUI::pluginStateChanged);
	connect(session.get(), &VSTPluginSession::automated, this, &VSTPluginFilterGUI::pluginStateChanged);
	connect(session.get(), &VSTPluginSession::sizeRequested, this, [this](int w, int h) {
		ui->frame->setFixedSize(w, h);
	});
}

VSTPluginFilterGUI::~VSTPluginFilterGUI()
{
	if (session->instance() != nullptr)
	{
		if (session->embedded())
			on_embedAction_toggled(false);
	}
}

void VSTPluginFilterGUI::store(QString& command, QString& parameters)
{
	command = "VSTPlugin";

	QString absolutePath = QString::fromStdWString(session->library()->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;

	if (relativePath.contains(" "))
		relativePath = "\"" + relativePath + "\"";
	parameters = "Library " + relativePath;

	// The Library token stays here because its relative/absolute resolution needs
	// Qt's QDir. The opaque bus contract and ChunkData-or-param body are produced
	// by the shared serializer (the same one the round-trip tests exercise).
	VSTPluginCommand cmd;
	cmd.chunkData = session->chunkData();
	cmd.paramMap = session->paramMap();
	cmd.stereoInput = stereoInput;
	if (document.bus().contract())
	{
		cmd.busContract = *document.bus().contract();
		cmd.hasBusContract = true;
		cmd.inputChannels = document.fill().inputFill();
		cmd.outputChannels = document.fill().outputFill();
	}
	parameters += QString::fromStdWString(cmd.serialize());
}

void VSTPluginFilterGUI::stereoInputToggled(bool checked)
{
	if (stereoInput == checked)
		return;
	stereoInput = checked;
	updateModel();
}

void VSTPluginFilterGUI::busLayoutPicked()
{
	const VST3BusLayout input = static_cast<VST3BusLayout>(ui->busInputComboBox->currentData().toInt());
	const VST3BusLayout output = static_cast<VST3BusLayout>(ui->busOutputComboBox->currentData().toInt());
	// A full Auto pair is the absence of a contract, so returning both
	// dropdowns to Auto removes the Input/Output keys from the line entirely.
	std::optional<VST3BusContract> picked;
	if (input != VST3BusLayout::Auto || output != VST3BusLayout::Auto)
		picked = VST3BusContract{input, output};
	const std::optional<VST3BusContract>& current = document.bus().contract();
	const bool same = current.has_value() == picked.has_value()
		&& (!picked || (current->input == picked->input && current->output == picked->output));
	if (same)
		return;
	// The document drops the fill of a side whose layout changed.
	if (picked)
		document.setLayouts(input, output);
	else
		document.clearLayouts();
	if (picked && stereoInput)
	{
		// The parser rejects StereoInput combined with Input/Output, so the
		// explicit contract silently retires the legacy flag.
		stereoInput = false;
		stereoInputAction->setChecked(false);
	}
	updateBusControls();
	updateFillRows();
	updateModel();
}

void VSTPluginFilterGUI::fillToggleClicked(bool checked)
{
	fillCollapsed = !checked;
	fillCollapsedFromPrefs = true;
	updateFillRows();
}

void VSTPluginFilterGUI::setChannelFlow(const ChannelFlowAtLine& flow)
{
	document.setSelectedChannels(flow.selected);
	updateFillRows();
}

void VSTPluginFilterGUI::updateFillRows()
{
	const VSTSlotFillModel& fillModel = document.fill();
	// A single rail never folds; the toggle quietly disappears with it.
	if (!fillModel.latchPresent())
		fillCollapsed = false;
	rebuildFillRow(false);
	rebuildFillRow(true);
	inputFillRow->setVisible(fillModel.railPresent(false));
	outputFillRow->setVisible(fillModel.railPresent(true) && !fillCollapsed);
}

void VSTPluginFilterGUI::rebuildFillRow(bool output)
{
	const VSTSlotFillModel& fillModel = document.fill();
	QWidget* row = output ? outputFillRow : inputFillRow;
	QHBoxLayout* box = static_cast<QHBoxLayout*>(row->layout());
	while (QLayoutItem* item = box->takeAt(0))
	{
		if (item->widget() != nullptr)
			item->widget()->deleteLater();
		delete item;
	}
	if (!output)
		fillToggle = nullptr;

	if (!output && fillModel.latchPresent())
	{
		fillToggle = new QCheckBox(tr("Channel fill"), row);
		fillToggle->setChecked(!fillCollapsed);
		fillToggle->setToolTip(tr("Choose which channels occupy the negotiated bus slots."));
		connect(fillToggle, &QCheckBox::toggled, this, &VSTPluginFilterGUI::fillToggleClicked);
		box->addWidget(fillToggle);
	}
	else
	{
		box->addWidget(new QLabel(output ? tr("Output fill") : tr("Input fill"), row));
	}

	if (!fillCollapsed)
	{
		const int count = fillModel.slotCount(output);
		for (int slot = 0; slot < count; slot++)
		{
			box->addWidget(new QLabel(QString::fromStdWString(fillModel.slotRole(output, slot)), row));
			QComboBox* combo = new QComboBox(row);
			combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
			for (const std::wstring& name : fillModel.selectedChannels())
				combo->addItem(QString::fromStdWString(name), QString::fromStdWString(name));
			combo->addItem(output ? tr("Discard (-)") : tr("Silence (-)"), QStringLiteral("-"));
			const QString value = QString::fromStdWString(fillModel.slotValue(output, slot));
			int index = combo->findData(value);
			if (index < 0)
			{
				// A committed value the menu does not list stays visible. It is
				// marked red only when the engine's resolver cannot place it in
				// the selection (the engine would refuse it); a position number
				// or an alias such as SL for RL resolves and stays plain.
				combo->insertItem(0, value, value);
				if (fillModel.slotChannelMissing(output, slot))
					combo->setItemData(0, QBrush(Qt::red), Qt::ForegroundRole);
				index = 0;
			}
			combo->setCurrentIndex(index);
			connect(combo, &QComboBox::activated, this, [this, combo, output, slot](int picked)
			{
				document.pickSlot(output, slot, combo->itemData(picked).toString().toStdWString());
				updateFillRows();
				updateModel();
			});
			box->addWidget(combo);
		}
	}
	box->addStretch(1);
}

void VSTPluginFilterGUI::updateBusControls()
{
	const VSTBusModel& bus = document.bus();
	ui->busInputComboBox->setCurrentIndex(ui->busInputComboBox->findData(static_cast<int>(bus.input())));
	ui->busOutputComboBox->setCurrentIndex(ui->busOutputComboBox->findData(static_cast<int>(bus.output())));

	// The row has no separate repair affordance, so the dropdowns stay enabled
	// even for a loaded VST2 module; the tooltip carries the caveat instead.
	const bool loadedVst2 = session->instance() != nullptr && !session->library()->isVST3();
	const QString busToolTip = loadedVst2
		? tr("A VST2 plugin ignores the Input and Output layouts.") : QString();
	ui->busInputComboBox->setToolTip(busToolTip);
	ui->busOutputComboBox->setToolTip(busToolTip);

	stereoInputAction->setEnabled(!bus.contract());
	stereoInputAction->setToolTip(bus.contract()
		? tr("Not available while Input and Output layouts are set.")
		: tr("Use for upmixers that expand a stereo signal to multichannel."));
}

void VSTPluginFilterGUI::loadPreferences(const QVariantMap& prefs)
{
	session->setAutoApplyDialog(prefs.value("autoApplyDialog", true).toBool());

	if (prefs.contains("slotFillCollapsed"))
	{
		fillCollapsed = prefs.value("slotFillCollapsed").toBool();
		fillCollapsedFromPrefs = true;
		updateFillRows();
	}

	if (prefs.value("embed").toBool())
		// will also call initPlugin
		ui->embedAction->setChecked(true);
	else
		session->initPlugin();
}

void VSTPluginFilterGUI::storePreferences(QVariantMap& prefs)
{
	prefs.insert("embed", ui->embedAction->isChecked());
	prefs.insert("autoApplyDialog", session->autoApplyDialog());
	if (fillCollapsedFromPrefs)
		prefs.insert("slotFillCollapsed", fillCollapsed);
}

void VSTPluginFilterGUI::on_openPanelButton_clicked()
{
	// While the panel is embedded, this button is its close affordance; the
	// options-menu checkbox stays in sync because closing goes through it.
	if (ui->embedAction->isChecked())
	{
		ui->embedAction->setChecked(false);
		return;
	}

	session->initPlugin();
	session->openDialog(this);
}

void VSTPluginFilterGUI::pluginStateChanged()
{
	updateModel();
	updatePermissionWarning();
}

// The session's status as this row always showed it: the plugin name in
// black, a failure in red, on the label the embedded panel replaces (the
// embed toggle shows and hides the label).
void VSTPluginFilterGUI::showStatus()
{
	const VSTPluginSession::Status& status = session->status();
	const QColor color = status.critical ? QColor(Qt::red) : QColor(Qt::black);
	QPalette palette = ui->statusLabel->palette();
	palette.setColor(QPalette::Active, QPalette::WindowText, color);
	palette.setColor(QPalette::Inactive, QPalette::WindowText, color);
	ui->statusLabel->setPalette(palette);
	ui->statusLabel->setText(status.text);
	updateBusControls();
}

void VSTPluginFilterGUI::on_pathLineEdit_editingFinished()
{
	if (session->libraryDiffers(ui->pathLineEdit->text()))
	{
		if (session->instance() != nullptr && ui->embedAction->isChecked())
			on_embedAction_toggled(false);
		session->replaceLibrary(ui->pathLineEdit->text());

		updateModel();
		updatePermissionWarning();

		if (ui->embedAction->isChecked())
			on_embedAction_toggled(true);
	}
}

void VSTPluginFilterGUI::on_selectButton_clicked()
{
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QString lastDir = settings.value("vst/lastDir", "").toString();
	if (lastDir == "")
		lastDir = pluginsDir.absolutePath();

	QFileInfo fileInfo(lastDir);
	QString path = ui->pathLineEdit->text();
	if (path.length() > 0)
		fileInfo.setFile(pluginsDir, path);

	QFileDialog dialog(this, tr("Select VST plugin"), fileInfo.absoluteFilePath(), "*.dll *.vst3");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("VST plugins (*.dll *.vst3)"));
	GUIHelper::enableVst3BundleSelection(dialog);
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		ui->pathLineEdit->setText(FileReferenceController::displayPathForBaseDirectory(pluginsDir.absolutePath(), absolutePath));
		on_pathLineEdit_editingFinished();
	}
}

void VSTPluginFilterGUI::on_embedAction_toggled(bool checked)
{
	session->initPlugin();

	const bool enable = checked && session->instance() != nullptr;
	if (enable != session->embedded())
	{
		// The frame is shown before the session embeds into it; the status
		// label gives way to it, and both return when embedding failed
		// (reported through the status).
		ui->frame->setVisible(enable);
		ui->statusLabel->setVisible(!enable);
		if (!session->setEmbedded(enable, ui->frame))
		{
			ui->frame->setVisible(false);
			ui->statusLabel->setVisible(true);
		}
	}

	// A checked action without a live embed (plugin missing or crashed while
	// opening the panel) would claim a panel that is not shown; drop the
	// check so the button reads "Open panel" again. The recursive toggle is
	// a no-op because embedded already matches.
	if (checked && !session->embedded() && ui->embedAction->isChecked())
		ui->embedAction->setChecked(false);

	// The button stays visible while embedded - it is the way out. Hiding it
	// left the embed removable only through the options menu, which read as
	// "the panel cannot be closed".
	ui->openPanelButton->setText(session->embedded() ? tr("Close panel") : tr("Open panel"));
}

void VSTPluginFilterGUI::updatePermissionWarning()
{
	// Evaluated from the library path and the saved chunk alone. Gated on a
	// loaded plugin instance, the warning appeared when a panel opened and
	// silently vanished on the next row rebuild, while the file stayed
	// unreadable for the audio service - a real problem reading as a false
	// alarm.
	if (session->library()->getLibPath().empty())
	{
		ui->warningTextEdit->setVisible(false);
		return;
	}

	QString text = session->libraryPermissionWarning();
	if (text.isEmpty())
		text = session->chunkPermissionWarning();

	if (text.isEmpty())
	{
		ui->warningTextEdit->setVisible(false);
		ui->warningTextEdit->setPlainText("");
	}
	else
	{
		ui->warningTextEdit->setPlainText(text);
		QSize textSize = ui->warningTextEdit->fontMetrics().size(0, text);
		ui->warningTextEdit->setFixedSize(textSize + QSize(40, 15));
		ui->warningTextEdit->setVisible(true);
	}
}
