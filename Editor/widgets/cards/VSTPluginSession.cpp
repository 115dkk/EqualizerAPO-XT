/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Logic moved from Editor/guis/VSTPluginFilterGUI.cpp (Copyright (C) 2017
	Jonas Thedering) and its port in VSTCardEditor.cpp.
*/

#include "VSTPluginSession.h"

#include <QAbstractEventDispatcher>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "services/security/AudioEngineAccess.h"
#include "Editor/guis/VSTPluginFilterGUIDialog.h"
#include "Editor/helpers/VstChunkScan.h"

using std::unordered_map;
using std::wstring;

VSTPluginSession::VSTPluginSession(Row row, std::shared_ptr<VSTPluginLibrary> library, wstring chunkData,
	unordered_map<wstring, float> paramMap)
	: row(row), pluginLibrary(std::move(library)), currentChunkData(std::move(chunkData)),
	currentParamMap(std::move(paramMap))
{
}

VSTPluginSession::~VSTPluginSession()
{
	previewFeeder.stop();
	releasePanelProcessing();
}

const std::shared_ptr<VSTPluginLibrary>& VSTPluginSession::library() const
{
	return pluginLibrary;
}

VSTPluginInstance* VSTPluginSession::instance() const
{
	return effect.get();
}

const wstring& VSTPluginSession::chunkData() const
{
	return currentChunkData;
}

const unordered_map<wstring, float>& VSTPluginSession::paramMap() const
{
	return currentParamMap;
}

const VSTPluginSession::Status& VSTPluginSession::status() const
{
	return currentStatus;
}

bool VSTPluginSession::embedded() const
{
	return isEmbedded;
}

bool VSTPluginSession::autoApplyDialog() const
{
	return autoApply;
}

void VSTPluginSession::setAutoApplyDialog(bool value)
{
	autoApply = value;
}

void VSTPluginSession::initPlugin()
{
	if (effect != nullptr)
		return;

	// Each text keeps the translation context of the row it was written for;
	// the card shows a missing library as a state, not as text.
	const bool card = row == Row::Card;
	currentStatus = Status();
	if (pluginLibrary->getLibPath() == L"")
	{
		currentStatus.libraryMissing = true;
		currentStatus.critical = true;
		if (!card)
			currentStatus.text = QCoreApplication::translate("VSTPluginFilterGUI", "No file selected.");
	}
	else
	{
		int result = pluginLibrary->initialize();
		if (result < 0)
		{
			currentStatus.critical = true;

			switch (result)
			{
			case AbstractLibrary::FILE_NOT_FOUND:
				currentStatus.libraryMissing = true;
				if (!card)
					currentStatus.text = QCoreApplication::translate("VSTPluginFilterGUI", "File not found.");
				break;
			case AbstractLibrary::LOADING_FAILED:
				currentStatus.text = card
					? QCoreApplication::translate("VSTCardEditor", "Library could not be loaded.")
					: QCoreApplication::translate("VSTPluginFilterGUI", "Library could not be loaded.");
				break;
			case AbstractLibrary::FUNCTIONS_MISSING:
				currentStatus.text = card
					? QCoreApplication::translate("VSTCardEditor", "Library does not contain needed functions.")
					: QCoreApplication::translate("VSTPluginFilterGUI", "Library does not contain needed functions.");
				break;
			case AbstractLibrary::WRONG_ARCHITECTURE:
			{
#ifdef _WIN64
				int bitDepth = 64;
#else
				int bitDepth = 32;
#endif
				currentStatus.text = (card
					? QCoreApplication::translate("VSTCardEditor", "Library has the wrong architecture. Only %1-bit libraries are supported.")
					: QCoreApplication::translate("VSTPluginFilterGUI", "Library has the wrong architecture. Only %1-bit libraries are supported."))
					.arg(bitDepth);
				break;
			}
			}
		}
		else
		{
			effect = std::make_unique<VSTPluginInstance>(pluginLibrary, 1);
			if (effect->initialize())
			{
				effect->setLanguage(QLocale().language() == QLocale::German ? 2 : 1);
				effect->setAutomateFunc([this]() { onAutomate(); });

				currentStatus.text = QString::fromStdWString(effect->getName());
			}
			else
			{
				effect.reset();

				currentStatus.critical = true;
				currentStatus.text = card
					? QCoreApplication::translate("VSTCardEditor", "Plugin crashed during initialization.")
					: QCoreApplication::translate("VSTPluginFilterGUI", "Plugin crashed during initialization.");
			}
		}
	}

	emit statusChanged();
}

bool VSTPluginSession::libraryDiffers(const QString& writtenPath) const
{
	return QString::fromStdWString(pluginLibrary->getLibPath()) != writtenPath;
}

void VSTPluginSession::replaceLibrary(const QString& writtenPath)
{
	int oldId = 0;
	if (effect != nullptr)
	{
		oldId = effect->uniqueID();
		effect.reset();
	}

	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString path = writtenPath;
	if (path.length() > 0)
		path = QDir::toNativeSeparators(QFileInfo(pluginsDir, writtenPath).absoluteFilePath());
	pluginLibrary = VSTPluginLibrary::getInstance(path.toStdWString());
	initPlugin();

	if (effect == nullptr || oldId == 0 || effect->uniqueID() != oldId)
	{
		currentChunkData = L"";
		currentParamMap.clear();
	}
}

void VSTPluginSession::openDialog(QWidget* dialogParent)
{
	if (effect == nullptr)
		return;

	effect->writeToEffect(currentChunkData, currentParamMap);

	// Before the dialog's startEditing: the feed prepares the instance for
	// the loopback mix format, which must happen while the processor is
	// still deactivated.
	previewFeeder.start(effect.get());

	VSTPluginFilterGUIDialog dialog(dialogParent, effect.get(), autoApply);
	if (!dialog.isEditorOpen())
	{
		// Same report as a failed embed, instead of an empty dialog.
		previewFeeder.stop();
		reportPanelCrash();
		return;
	}
	acquirePanelProcessing();
	connect(dialog.getApplyButton(), &QPushButton::pressed, this, &VSTPluginSession::applyDialog);
	connect(dialog.getAutoApplyCheckBox(), &QCheckBox::toggled, this, &VSTPluginSession::setAutoApplyDialog);
	connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock,
		this, &VSTPluginSession::onIdle);

	if (dialog.exec() == QDialog::Accepted)
	{
		effect->readFromEffect(currentChunkData, currentParamMap);
		emit stateChanged();
	}
	disconnect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock,
		this, &VSTPluginSession::onIdle);
	previewFeeder.stop();
	releasePanelProcessing();
}

bool VSTPluginSession::setEmbedded(bool enable, QWidget* host)
{
	if (effect == nullptr)
		enable = false;
	if (enable == isEmbedded)
		return isEmbedded;

	isEmbedded = enable;
	if (enable)
	{
		// Before embedPlugin()'s startEditing, for the same deactivation
		// contract as the dialog path.
		previewFeeder.start(effect.get());

		if (embedPlugin(host))
		{
			acquirePanelProcessing();
			effect->setSizeWindowFunc([this](int width, int height) {
				if (isEmbedded)
					emit sizeRequested(width, height);
			});
			connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock,
				this, &VSTPluginSession::onIdle);
		}
		else
		{
			previewFeeder.stop();
			isEmbedded = false;
			reportPanelCrash();
		}
	}
	else
	{
		previewFeeder.stop();
		if (effect != nullptr)
		{
			releasePanelProcessing();
			effect->stopEditing();
			effect->setSizeWindowFunc(nullptr);
		}
		disconnect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock,
			this, &VSTPluginSession::onIdle);
	}
	return isEmbedded;
}

QString VSTPluginSession::libraryPermissionWarning() const
{
	if (pluginLibrary->getLibPath().empty()
		|| AudioEngineAccess::isReadableByAudioEngine(pluginLibrary->getLibPath()))
		return QString();
	return QCoreApplication::translate("VSTPluginFilterGUI",
		"The library is not readable by the audio service.\nChange the file permissions or copy the file to the VSTPlugins directory.");
}

// Evaluated from the saved plug-in state alone, never gated on a loaded
// instance: gated on one, the warning appeared when a panel opened and
// silently vanished on the next row rebuild, while the files stayed
// unreadable for the audio service - a real problem reading as a false
// alarm.
QString VSTPluginSession::chunkPermissionWarning() const
{
	const QStringList files = vstChunkUnreadablePaths(currentChunkData);
	if (files.isEmpty())
		return QString();
	return (row == Row::Card
		? QCoreApplication::translate("VSTCardEditor", "The plugin seemingly accesses these files not readable by the audio service:\n"
			"%0\n"
			"Change the file permissions or copy the files to the config directory.")
		: QCoreApplication::translate("VSTPluginFilterGUI", "The plugin seemingly accesses these files not readable by the audio service:\n"
			"%0\n"
			"Change the file permissions or copy the files to the config directory."))
		.arg(files.join("\n"));
}

void VSTPluginSession::applyDialog()
{
	effect->readFromEffect(currentChunkData, currentParamMap);
	emit stateChanged();
}

void VSTPluginSession::onIdle()
{
	if (effect == nullptr)
		return;

	effect->doIdle();

	if (isEmbedded || autoApply)
	{
		if (!lastReadTimer.isValid() || lastReadTimer.elapsed() > 1000)
		{
			wstring newChunkData;
			unordered_map<wstring, float> newParamMap;
			effect->readFromEffect(newChunkData, newParamMap);
			if (newChunkData != currentChunkData || newParamMap != currentParamMap)
			{
				currentChunkData = newChunkData;
				currentParamMap = newParamMap;
				emit stateChanged();
			}
			lastReadTimer.restart();
		}
	}
}

void VSTPluginSession::onAutomate()
{
	if (isEmbedded || autoApply)
	{
		effect->readFromEffect(currentChunkData, currentParamMap);
		emit automated();
	}
}

bool VSTPluginSession::embedPlugin(QWidget* host)
{
	bool result = true;

	__try
	{
		effect->writeToEffect(currentChunkData, currentParamMap);

		HWND hwnd = (HWND)host->winId();
		short width = 0, height = 0;

		// startEditing also fails without an exception (no view, attach
		// refused); unchecked, that embedded its 400x300 placeholder size as
		// an empty frame and reported the panel as open.
		result = effect->startEditing(hwnd, &width, &height, host->devicePixelRatioF());

		if (result)
			host->setFixedSize(width, height);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		result = false;
	}

	return result;
}

void VSTPluginSession::acquirePanelProcessing()
{
	if (effect == nullptr || effect->canProcessNow())
		return;
	effect->startProcessing();
	ownsPanelProcessing = effect->canProcessNow();
}

void VSTPluginSession::releasePanelProcessing()
{
	if (!ownsPanelProcessing || effect == nullptr)
		return;
	effect->stopProcessingSafely();
	ownsPanelProcessing = false;
}

void VSTPluginSession::reportPanelCrash()
{
	currentStatus.critical = true;
	currentStatus.libraryMissing = false;
	currentStatus.text = row == Row::Card
		? QCoreApplication::translate("VSTCardEditor", "Plugin crashed when opening panel.")
		: QCoreApplication::translate("VSTPluginFilterGUI", "Plugin crashed when opening panel.");
	emit statusChanged();
}
