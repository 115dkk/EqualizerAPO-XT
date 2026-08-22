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
#include <QFileDialog>
#include <QSettings>
#include <QAbstractEventDispatcher>
#include <QAction>
#include <QMenu>
#include <QStringList>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "services/security/AudioEngineAccess.h"
#include "filters/VSTPluginCommand.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/helpers/VstChunkScan.h"
#include "Editor/MainWindow.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "VSTPluginFilterGUIDialog.h"
#include "VSTPluginFilterGUI.h"
#include "ui_VSTPluginFilterGUI.h"

using std::bind;
using std::replace;
using std::string;
using std::unordered_map;
using std::wstring;
using std::placeholders::_1;
using std::placeholders::_2;

VSTPluginFilterGUI::VSTPluginFilterGUI(std::shared_ptr<VSTPluginLibrary> library, const std::wstring& chunkData, const std::unordered_map<std::wstring, float>& paramMap,
	bool stereoInput, const std::optional<VST3BusContract>& busContract,
	std::vector<std::wstring> inputChannels, std::vector<std::wstring> outputChannels)
	: ui(std::make_unique<Ui::VSTPluginFilterGUI>()), library(library), chunkData(chunkData), paramMap(paramMap),
	stereoInput(stereoInput), busContract(busContract),
	inputChannels(std::move(inputChannels)), outputChannels(std::move(outputChannels))
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
	static const VST3BusLayout busLayoutChoices[] = {
		VST3BusLayout::Auto, VST3BusLayout::Mono, VST3BusLayout::Stereo,
		VST3BusLayout::Surround40, VST3BusLayout::Surround41, VST3BusLayout::Surround50,
		VST3BusLayout::Surround51, VST3BusLayout::Surround61, VST3BusLayout::Surround71,
		VST3BusLayout::Surround712, VST3BusLayout::Surround714};
	for (VST3BusLayout layout : busLayoutChoices)
	{
		const QString name = QString::fromWCharArray(vst3BusLayoutName(layout));
		ui->busInputComboBox->addItem(name, static_cast<int>(layout));
		ui->busOutputComboBox->addItem(name, static_cast<int>(layout));
	}
	connect(ui->busInputComboBox, &QComboBox::activated, this, &VSTPluginFilterGUI::busLayoutPicked);
	connect(ui->busOutputComboBox, &QComboBox::activated, this, &VSTPluginFilterGUI::busLayoutPicked);
	updateBusControls();

	// Frozen legacy row: it stays functional under every skin, so it consults
	// the same chrome hook as the card editors (legacyRow marks it for skins
	// that want to leave the legacy path untouched).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("vst");
	rowInfo.command = QStringLiteral("vstplugin");
	rowInfo.legacyRow = true;
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);
}

VSTPluginFilterGUI::~VSTPluginFilterGUI()
{
	if (effect != nullptr)
	{
		if (embedded)
			on_embedAction_toggled(false);
	}
}

void VSTPluginFilterGUI::store(QString& command, QString& parameters)
{
	command = "VSTPlugin";

	QString absolutePath = QString::fromStdWString(library->getLibPath());
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
	cmd.chunkData = chunkData;
	cmd.paramMap = paramMap;
	cmd.stereoInput = stereoInput;
	if (busContract)
	{
		cmd.busContract = *busContract;
		cmd.hasBusContract = true;
		cmd.inputChannels = inputChannels;
		cmd.outputChannels = outputChannels;
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
	const bool same = busContract.has_value() == picked.has_value()
		&& (!picked || (busContract->input == picked->input && busContract->output == picked->output));
	if (same)
		return;
	// A changed layout invalidates that side's per-slot channel fill: the
	// slot count no longer matches, so the stale list would fail to parse.
	if (input != (busContract ? busContract->input : VST3BusLayout::Auto))
		inputChannels.clear();
	if (output != (busContract ? busContract->output : VST3BusLayout::Auto))
		outputChannels.clear();
	busContract = picked;
	if (busContract && stereoInput)
	{
		// The parser rejects StereoInput combined with Input/Output, so the
		// explicit contract silently retires the legacy flag.
		stereoInput = false;
		stereoInputAction->setChecked(false);
	}
	updateBusControls();
	updateModel();
}

void VSTPluginFilterGUI::updateBusControls()
{
	ui->busInputComboBox->setCurrentIndex(ui->busInputComboBox->findData(
		static_cast<int>(busContract ? busContract->input : VST3BusLayout::Auto)));
	ui->busOutputComboBox->setCurrentIndex(ui->busOutputComboBox->findData(
		static_cast<int>(busContract ? busContract->output : VST3BusLayout::Auto)));

	// The row has no separate repair affordance, so the dropdowns stay enabled
	// even for a loaded VST2 module; the tooltip carries the caveat instead.
	const bool loadedVst2 = effect != nullptr && !library->isVST3();
	const QString busToolTip = loadedVst2
		? tr("A VST2 plugin ignores the Input and Output layouts.") : QString();
	ui->busInputComboBox->setToolTip(busToolTip);
	ui->busOutputComboBox->setToolTip(busToolTip);

	stereoInputAction->setEnabled(!busContract);
	stereoInputAction->setToolTip(busContract
		? tr("Not available while Input and Output layouts are set.")
		: tr("Use for upmixers that expand a stereo signal to multichannel."));
}

void VSTPluginFilterGUI::loadPreferences(const QVariantMap& prefs)
{
	autoApplyDialog = prefs.value("autoApplyDialog").toBool();

	if (prefs.value("embed").toBool())
		// will also call initPlugin
		ui->embedAction->setChecked(true);
	else
		initPlugin();
}

void VSTPluginFilterGUI::storePreferences(QVariantMap& prefs)
{
	prefs.insert("embed", ui->embedAction->isChecked());
	prefs.insert("autoApplyDialog", autoApplyDialog);
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

	initPlugin();

	if (effect != nullptr)
	{
		effect->writeToEffect(chunkData, paramMap);

		VSTPluginFilterGUIDialog dialog(this, effect.get(), autoApplyDialog);
		connect(dialog.getApplyButton(), SIGNAL(pressed()), SLOT(applyDialog()));
		connect(dialog.getAutoApplyCheckBox(), SIGNAL(toggled(bool)), SLOT(autoApplyToggled(bool)));
		connect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), SLOT(on_idle()));

		if (dialog.exec() == QDialog::Accepted)
		{
			effect->readFromEffect(chunkData, paramMap);
			updateModel();
			updatePermissionWarning();
		}
		disconnect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), this, SLOT(on_idle()));
	}
}

void VSTPluginFilterGUI::applyDialog()
{
	effect->readFromEffect(chunkData, paramMap);
	updateModel();
	updatePermissionWarning();
}

void VSTPluginFilterGUI::autoApplyToggled(bool checked)
{
	autoApplyDialog = checked;
}

void VSTPluginFilterGUI::initPlugin()
{
	if (effect != nullptr)
		return;

	QColor color;
	QString text;
	if (library->getLibPath() == L"")
	{
		text = tr("No file selected.");
		color = Qt::red;
	}
	else
	{
		int result = library->initialize();
		if (result < 0)
		{
			color = Qt::red;

			switch (result)
			{
			case AbstractLibrary::FILE_NOT_FOUND:
				text = tr("File not found.");
				break;
			case AbstractLibrary::LOADING_FAILED:
				text = tr("Library could not be loaded.");
				break;
			case AbstractLibrary::FUNCTIONS_MISSING:
				text = tr("Library does not contain needed functions.");
				break;
			case AbstractLibrary::WRONG_ARCHITECTURE:
#ifdef _WIN64
				int bitDepth = 64;
#else
				int bitDepth = 32;
#endif
				text = tr("Library has the wrong architecture. Only %1-bit libraries are supported.").arg(bitDepth);
				break;
			}
		}
		else
		{
			effect = std::make_unique<VSTPluginInstance>(library, 1);
			if (effect->initialize())
			{
				effect->setLanguage(QLocale().language() == QLocale::German ? 2 : 1);
				effect->setAutomateFunc(bind(&VSTPluginFilterGUI::onAutomate, this));

				color = Qt::black;
				text = QString::fromStdWString(effect->getName());
			}
			else
			{
				effect.reset();

				color = Qt::red;
				text = tr("Plugin crashed during initialization.");
			}
		}
	}

	QPalette palette = ui->statusLabel->palette();
	palette.setColor(QPalette::Active, QPalette::WindowText, color);
	palette.setColor(QPalette::Inactive, QPalette::WindowText, color);
	ui->statusLabel->setPalette(palette);
	ui->statusLabel->setText(text);
	updateBusControls();
}

void VSTPluginFilterGUI::on_pathLineEdit_editingFinished()
{
	if (QString::fromStdWString(library->getLibPath()) != ui->pathLineEdit->text())
	{
		int oldId = 0;
		if (effect != nullptr)
		{
			oldId = effect->uniqueID();
			if (ui->embedAction->isChecked())
				on_embedAction_toggled(false);
			effect.reset();
		}

		QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
		QString path = ui->pathLineEdit->text();
		if (path.length() > 0)
			path = QDir::toNativeSeparators(QFileInfo(pluginsDir, ui->pathLineEdit->text()).absoluteFilePath());
		library = VSTPluginLibrary::getInstance(path.toStdWString());
		initPlugin();

		if (effect == nullptr || oldId == 0 || effect->uniqueID() != oldId)
		{
			chunkData = L"";
			paramMap.clear();
		}

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
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		QString relativePath = pluginsDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith("../../"))
			relativePath = absolutePath;
		ui->pathLineEdit->setText(QDir::toNativeSeparators(relativePath));
		on_pathLineEdit_editingFinished();
	}
}

void VSTPluginFilterGUI::on_embedAction_toggled(bool checked)
{
	initPlugin();

	bool enable = checked;
	if (effect == nullptr)
		enable = false;

	if (enable != embedded)
	{
		embedded = enable;
		ui->frame->setVisible(enable);
		ui->statusLabel->setVisible(!enable);

		if (enable)
		{
			if (embedPlugin())
			{
				effect->setSizeWindowFunc(bind(&VSTPluginFilterGUI::onSizeWindow, this, _1, _2));
				connect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), SLOT(on_idle()));
			}
			else
			{
				embedded = false;
				ui->frame->setVisible(false);
				ui->statusLabel->setVisible(true);

				QPalette palette = ui->statusLabel->palette();
				palette.setColor(QPalette::Active, QPalette::WindowText, Qt::red);
				palette.setColor(QPalette::Inactive, QPalette::WindowText, Qt::red);
				ui->statusLabel->setPalette(palette);
				ui->statusLabel->setText(tr("Plugin crashed when opening panel."));
			}
		}
		else
		{
			if (effect != nullptr)
			{
				effect->stopEditing();
				effect->setSizeWindowFunc(nullptr);
			}

			disconnect(QAbstractEventDispatcher::instance(), SIGNAL(aboutToBlock()), this, SLOT(on_idle()));
		}
	}

	// A checked action without a live embed (plugin missing or crashed while
	// opening the panel) would claim a panel that is not shown; drop the
	// check so the button reads "Open panel" again. The recursive toggle is
	// a no-op because embedded already matches.
	if (checked && !embedded && ui->embedAction->isChecked())
		ui->embedAction->setChecked(false);

	// The button stays visible while embedded - it is the way out. Hiding it
	// left the embed removable only through the options menu, which read as
	// "the panel cannot be closed".
	ui->openPanelButton->setText(embedded ? tr("Close panel") : tr("Open panel"));
}

void VSTPluginFilterGUI::on_idle()
{
	if (effect != nullptr)
	{
		effect->doIdle();

		if (embedded || autoApplyDialog)
		{
			if (!lastReadTimer.isValid() || lastReadTimer.elapsed() > 1000)
			{
				wstring newChunkData;
				unordered_map<std::wstring, float> newParamMap;
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

void VSTPluginFilterGUI::onAutomate()
{
	if (embedded || autoApplyDialog)
	{
		effect->readFromEffect(chunkData, paramMap);
		updateModel();
		updatePermissionWarning();
	}
}

void VSTPluginFilterGUI::onSizeWindow(int w, int h)
{
	if (embedded)
		ui->frame->setFixedSize(w, h);
}

bool VSTPluginFilterGUI::embedPlugin()
{
	bool result = true;

	__try
	{
		effect->writeToEffect(chunkData, paramMap);

		HWND hwnd = (HWND)ui->frame->winId();
		short width, height;

		effect->startEditing(hwnd, &width, &height, ui->frame->devicePixelRatioF());

		ui->frame->setFixedSize(width, height);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		result = false;
	}

	return result;
}

void VSTPluginFilterGUI::updatePermissionWarning()
{
	// Evaluated from the library path and the saved chunk alone. Gated on a
	// loaded plugin instance, the warning appeared when a panel opened and
	// silently vanished on the next row rebuild, while the file stayed
	// unreadable for the audio service - a real problem reading as a false
	// alarm.
	if (library->getLibPath().empty())
	{
		ui->warningTextEdit->setVisible(false);
		return;
	}

	if (!AudioEngineAccess::isReadableByAudioEngine(library->getLibPath()))
	{
		QString text = tr("The library is not readable by the audio service.\nChange the file permissions or copy the file to the VSTPlugins directory.");

		ui->warningTextEdit->setPlainText(text);
		QSize textSize = ui->warningTextEdit->fontMetrics().size(0, text);
		ui->warningTextEdit->setFixedSize(textSize + GUIHelper::scale(QSize(40, 15)));
		ui->warningTextEdit->setVisible(true);
		return;
	}

	const QStringList files = vstChunkUnreadablePaths(chunkData);

	if (files.isEmpty())
	{
		ui->warningTextEdit->setVisible(false);
		ui->warningTextEdit->setPlainText("");
	}
	else
	{
		QString text = tr("The plugin seemingly accesses these files not readable by the audio service:\n"
				"%0\n"
				"Change the file permissions or copy the files to the config directory.").arg(files.join("\n"));
		ui->warningTextEdit->setPlainText(text);
		QSize textSize = ui->warningTextEdit->fontMetrics().size(0, text);
		ui->warningTextEdit->setFixedSize(textSize + GUIHelper::scale(QSize(40, 15)));
		ui->warningTextEdit->setVisible(true);
	}
}
