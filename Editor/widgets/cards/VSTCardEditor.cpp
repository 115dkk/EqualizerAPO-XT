/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Logic ported from Editor/guis/VSTPluginFilterGUI.cpp (Copyright (C) 2017
	Jonas Thedering) into a card-native layout; store()/parse round-trip verified
	lossless by --selftest-vst. See VSTCardEditor.h for the presentation.
*/

#include "VSTCardEditor.h"

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

#include "helpers/aeffectx.h"
#include "helpers/StringHelper.h"
#include "helpers/RegistryHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/MainWindow.h"
#include "Editor/guis/VSTPluginFilterGUIDialog.h"
#include "ReferenceCardView.h"

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
}

VSTCardEditor::VSTCardEditor(shared_ptr<VSTPluginLibrary> library, const wstring& chunkData,
	const unordered_map<wstring, float>& paramMap, QWidget* parent)
	: IFilterGUI(parent), library(library), chunkData(chunkData), paramMap(paramMap)
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
	connect(openPanelButton, SIGNAL(clicked()), this, SLOT(openPanel()));
	view->addActionButton(ReferenceCardView::ActionRole::OpenPanel, openPanelButton);

	optionsButton = new QToolButton(view);
	optionsButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	optionsButton->setText(QStringLiteral("..."));
	optionsButton->setPopupMode(QToolButton::InstantPopup);
	QMenu* menu = new QMenu(optionsButton);
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

	displayPath = displayPathForLibrary(library->getLibPath());

	// Let the active skin decorate this VST body (the row is recreated on
	// skin switches, so construction is the only moment needed).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("vst");
	rowInfo.command = QStringLiteral("vstplugin");
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	updateReferenceState();
	updatePermissionWarning();
}

VSTCardEditor::~VSTCardEditor()
{
	if (effect != nullptr)
	{
		if (embedded)
			embedToggled(false);
		delete effect;
		effect = nullptr;
	}
}

void VSTCardEditor::store(QString& command, QString& parameters)
{
	command = "VSTPlugin";

	QString relativePath = displayPathForLibrary(library->getLibPath());

	if (relativePath.contains(" "))
		relativePath = "\"" + relativePath + "\"";
	parameters = "Library " + relativePath;

	if (chunkData != L"")
	{
		parameters += " ChunkData \"" + QString::fromStdWString(chunkData) + "\"";
	}
	else
	{
		for (auto it : paramMap)
		{
			QString name = QString::fromStdWString(it.first);
			if (name.contains(" ") || name.contains("\""))
				name = "\"" + name.replace("\"", "\"\"") + "\"";
			parameters += " " + name + " " + QString("%1").arg(it.second);
		}
	}
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
	initPlugin();
	updateReferenceState();

	if (effect != nullptr)
	{
		effect->writeToEffect(chunkData, paramMap);

		VSTPluginFilterGUIDialog dialog(this, effect, autoApplyDialog);
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
		return;

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
			effect = new VSTPluginInstance(library, 1);
			if (effect->initialize())
			{
				effect->setLanguage(QLocale().language() == QLocale::German ? 2 : 1);
				effect->setAutomateFunc([this]() { onAutomate(); });
			}
			else
			{
				delete effect;
				effect = nullptr;
				initErrorText = tr("Plugin crashed during initialization.");
			}
		}
	}
}

// Map the library / plugin lifecycle onto the reference-card state: the
// loaded plugin's display name first, the file name as the fallback identity,
// the broken library as the missing transition with Locate as recovery.
void VSTCardEditor::updateReferenceState()
{
	ReferenceCardState state;
	state.kind = QStringLiteral("vst");
	state.editText = displayPath;

	if (displayPath.isEmpty())
	{
		state.missing = true;
		state.name = tr("No plugin selected");
	}
	else
	{
		const QString normalized = QDir::fromNativeSeparators(displayPath);
		const QFileInfo asWritten(normalized);
		state.name = asWritten.fileName();
		state.absolutePath = QDir::isAbsolutePath(normalized);
		state.fullPath = QDir::toNativeSeparators(QString::fromStdWString(library->getLibPath()));

		const QString suffix = asWritten.suffix().toLower();
		if (suffix == QStringLiteral("vst3"))
			state.formatBadge = QStringLiteral("VST3");
		else if (suffix == QStringLiteral("dll"))
			state.formatBadge = QStringLiteral("VST2");

		if (state.absolutePath)
			state.directory = QDir::toNativeSeparators(QFileInfo(QString::fromStdWString(library->getLibPath())).absolutePath());
		else if (asWritten.path() != QStringLiteral("."))
			state.directory = QDir::toNativeSeparators(asWritten.path());

		state.missing = libraryMissing;
		if (effect != nullptr)
		{
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

	const bool locate = state.missing && !displayPath.isEmpty();
	selectButton->setText(locate ? tr("Locate...") : QString());
	selectButton->setToolTip(locate ? tr("Locate the missing plugin library") : tr("Select VST plugin"));
	openPanelButton->setEnabled(!state.missing && !displayPath.isEmpty());

	view->setState(state);
}

void VSTCardEditor::pathCommitted(const QString& text)
{
	displayPath = text;
	if (QString::fromStdWString(library->getLibPath()) != text)
	{
		int oldId = 0;
		if (effect != nullptr)
		{
			oldId = effect->uniqueID();
			if (embedAction->isChecked())
				embedToggled(false);
			delete effect;
			effect = nullptr;
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
	if (displayPath.length() > 0)
		fileInfo.setFile(pluginsDir, displayPath);

	QFileDialog dialog(this, tr("Select VST plugin"), fileInfo.absoluteFilePath(), "*.dll *.vst3");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("VST plugins (*.dll *.vst3)"));
	if (displayPath.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		QString relativePath = pluginsDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith("../../"))
			relativePath = absolutePath;
		pathCommitted(QDir::toNativeSeparators(relativePath));
	}
}

void VSTCardEditor::embedToggled(bool checked)
{
	initPlugin();
	updateReferenceState();

	openPanelButton->setVisible(!checked);

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

	ACCESS_MASK mask = GENERIC_READ;
	try
	{
		mask = RegistryHelper::getFileAccessForUser(library->getLibPath(), SECURITY_LOCAL_SERVICE_RID);
	}
	catch (const RegistryException&)
	{
		// ignore
	}

	if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
	{
		QString text = tr("The library is not readable by the audio service.\nChange the file permissions or copy the file to the VSTPlugins directory.");
		warningTextEdit->setPlainText(text);
		QSize textSize = warningTextEdit->fontMetrics().size(0, text);
		warningTextEdit->setFixedSize(textSize + GUIHelper::scale(QSize(40, 15)));
		warningTextEdit->setVisible(true);
		return;
	}

	QStringList files;
	if (chunkData != L"" && chunkData.length() < 100000)
	{
		QByteArray bytes = QByteArray::fromBase64(QString::fromStdWString(chunkData).toUtf8());
		QString string = QString::fromUtf8(bytes.data(), bytes.length());
		QRegularExpression regexp("[A-Za-z]:(?:\\\\[\\w \\(\\)-]+)+\\.[A-Za-z]{3}");
		QRegularExpressionMatchIterator it = regexp.globalMatch(string);
		while (it.hasNext())
		{
			QRegularExpressionMatch m = it.next();
			QString path = m.captured();
			QFile file(path);
			if (file.exists())
			{
				ACCESS_MASK fileMask = GENERIC_READ;
				try
				{
					fileMask = RegistryHelper::getFileAccessForUser(path.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
				}
				catch (const RegistryException&)
				{
					// ignore
				}
				if ((fileMask & GENERIC_READ) != GENERIC_READ && (fileMask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
					files.append(path);
			}
		}
	}

	if (files.isEmpty())
	{
		warningTextEdit->setVisible(false);
		warningTextEdit->setPlainText("");
	}
	else
	{
		files.removeDuplicates();
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
#include "helpers/VSTPluginLibrary.h"

REGISTER_FILTER_CARD_EDITOR(vstplugin, [](FilterTable*, const QString&, const QString& parameters) -> IFilterGUI* {
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
		editor = new VSTCardEditor(filter->getLibrary(), filter->getChunkData(), filter->getParamMap());
	}
	else
	{
		editor = new VSTCardEditor(VSTPluginLibrary::getInstance(L""), L"", std::unordered_map<std::wstring, float>());
	}
	return editor;
})
