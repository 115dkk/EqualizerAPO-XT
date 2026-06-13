/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	Logic ported from Editor/guis/VSTPluginFilterGUI.cpp (Copyright (C) 2017
	Jonas Thedering) into a card-native layout; store()/parse round-trip verified
	lossless by --selftest-vst.
*/

#include "VSTCardEditor.h"

#include <QAbstractEventDispatcher>
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
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
#include "Editor/widgets/cards/ReferenceCard.h"

using std::shared_ptr;
using std::unordered_map;
using std::wstring;

VSTCardEditor::VSTCardEditor(shared_ptr<VSTPluginLibrary> library, const wstring& chunkData,
	const unordered_map<wstring, float>& paramMap, QWidget* parent)
	: IFilterGUI(parent), library(library), chunkData(chunkData), paramMap(paramMap)
{
	setObjectName(QStringLiteral("VSTCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor actionColor(tokens.text);

	card = new ReferenceCard(QStringLiteral("vst"), this);
	connect(card, &ReferenceCard::nameActivated, this, &VSTCardEditor::openPanel);
	connect(card, &ReferenceCard::editRequested, this, [this]() { card->enterEditMode(); });
	connect(card, &ReferenceCard::pathEdited, this, &VSTCardEditor::pathChanged);

	// X-4: recovery entry point when the library is missing. Visible only then.
	locateButton = card->addActionButton(QStringLiteral("RefLocateAction"));
	locateButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), actionColor, 18));
	locateButton->setText(tr("Locate..."));
	locateButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	locateButton->setToolTip(tr("Locate the missing plugin library"));
	locateButton->setVisible(false);
	connect(locateButton, &QToolButton::clicked, this, &VSTCardEditor::locateFile);

	// X-2: the Open panel button stays, demoted to a secondary affordance next
	// to the clickable name.
	openPanelButton = new QPushButton(tr("Open panel"), card);
	openPanelButton->setObjectName(QStringLiteral("VSTCardPanelButton"));
	connect(openPanelButton, SIGNAL(clicked()), this, SLOT(openPanel()));
	card->addActionWidget(openPanelButton);

	optionsButton = card->addActionButton(QStringLiteral("VSTCardOptions"));
	optionsButton->setText(QStringLiteral("..."));
	optionsButton->setPopupMode(QToolButton::InstantPopup);
	QMenu* menu = new QMenu(optionsButton);
	embedAction = menu->addAction(tr("Embed panel in card"));
	embedAction->setCheckable(true);
	connect(embedAction, SIGNAL(toggled(bool)), this, SLOT(embedToggled(bool)));
	optionsButton->setMenu(menu);

	root->addWidget(card);

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

	// Let the active skin decorate this VST body (the row is recreated on
	// skin switches, so construction is the only moment needed).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("vst");
	rowInfo.command = QStringLiteral("vstplugin");
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	// X-1: resolve the display name once at card creation, fail-safe. Only probe
	// the binary when the library path actually resolves to an existing file;
	// for a missing reference we never attempt a load (per AR2: no attempt when
	// not found) and fall back to the file name.
	QString status;
	if (library->getLibPath() != L"")
	{
		QFileInfo libInfo(QString::fromStdWString(library->getLibPath()));
		if (libInfo.exists())
			initPlugin();
		else
			libraryMissing = true;
	}
	refreshCard(status);
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

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;

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
	refreshCard(QString());
}

void VSTCardEditor::storePreferences(QVariantMap& prefs)
{
	prefs.insert("embed", embedAction->isChecked());
	prefs.insert("autoApplyDialog", autoApplyDialog);
}

void VSTCardEditor::openPanel()
{
	initPlugin();

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

	libraryMissing = false;
	QString text;
	if (library->getLibPath() == L"")
	{
		text = tr("No file selected.");
		libraryMissing = true;
	}
	else
	{
		int result = library->initialize();
		if (result < 0)
		{
			if (result == AbstractLibrary::FILE_NOT_FOUND)
				libraryMissing = true;
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
				text = tr("Plugin crashed during initialization.");
			}
		}
	}

	refreshCard(text);
}

QString VSTCardEditor::displayPath() const
{
	QString absolutePath = QString::fromStdWString(library->getLibPath());
	if (absolutePath.isEmpty())
		return QString();
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;
	return relativePath;
}

QString VSTCardEditor::currentName() const
{
	// Prefer the plugin's own display name (X-1); fall back to the file stem.
	if (effect != nullptr)
	{
		QString name = QString::fromStdWString(effect->getName()).trimmed();
		if (!name.isEmpty())
			return name;
	}
	QString absolutePath = QString::fromStdWString(library->getLibPath());
	if (absolutePath.isEmpty())
		return tr("No plugin selected");
	return QFileInfo(absolutePath).fileName();
}

void VSTCardEditor::refreshCard(const QString& status)
{
	ReferenceCardState state;
	QString absolutePath = QString::fromStdWString(library->getLibPath());
	state.name = currentName();
	state.absolutePath = !absolutePath.isEmpty() && QDir::isAbsolutePath(QDir::fromNativeSeparators(displayPath()));

	if (!absolutePath.isEmpty())
	{
		QFileInfo libInfo(absolutePath);
		state.directory = QDir::toNativeSeparators(libInfo.absolutePath());
		state.fullPath = QDir::toNativeSeparators(libInfo.absoluteFilePath());
		state.formatBadge = library->isVST3() ? QStringLiteral("VST3") : QStringLiteral("VST2");
	}
	else
	{
		state.fullPath.clear();
	}

	state.missing = libraryMissing || absolutePath.isEmpty();
	if (state.missing)
	{
		state.statusText = absolutePath.isEmpty()
			? tr("No plugin selected")
			: tr("File not found - use Locate to reconnect");
		state.statusSeverity = absolutePath.isEmpty()
			? ReferenceCardState::Severity::Warning
			: ReferenceCardState::Severity::Critical;
		// Hide the format badge for a missing reference: we cannot trust it.
		state.formatBadge.clear();
	}
	else if (!status.isEmpty())
	{
		// A load error other than not-found (wrong arch, crashed, ...).
		state.statusText = status;
		state.statusSeverity = ReferenceCardState::Severity::Critical;
	}

	// The name opens the panel only when a plugin is actually loaded (X-1/X-2).
	state.nameClickable = effect != nullptr;

	card->setState(state);
	// Invalid actions are demoted when the reference is broken (X-3).
	const bool resolvable = !state.missing;
	openPanelButton->setEnabled(resolvable);
	optionsButton->setEnabled(resolvable);
	locateButton->setVisible(state.missing && !absolutePath.isEmpty());
}

void VSTCardEditor::pathChanged(const QString& newPath)
{
	if (QString::fromStdWString(library->getLibPath()) == newPath)
	{
		refreshCard(QString());
		return;
	}

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
	QString path = newPath;
	if (path.length() > 0)
		path = QDir::toNativeSeparators(QFileInfo(pluginsDir, newPath).absoluteFilePath());
	library = VSTPluginLibrary::getInstance(path.toStdWString());

	// Probe only when the resolved path exists (no attempt when not found).
	libraryMissing = false;
	if (library->getLibPath() != L"" && QFileInfo(QString::fromStdWString(library->getLibPath())).exists())
		initPlugin();
	else if (library->getLibPath() != L"")
		libraryMissing = true;

	if (effect == nullptr || oldId == 0 || effect->uniqueID() != oldId)
	{
		chunkData = L"";
		paramMap.clear();
	}

	refreshCard(QString());
	updateModel();
	updatePermissionWarning();

	if (embedAction->isChecked())
		embedToggled(true);
}

void VSTCardEditor::locateFile()
{
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QString lastDir = settings.value("vst/lastDir", "").toString();
	if (lastDir == "")
		lastDir = pluginsDir.absolutePath();

	QFileInfo fileInfo(lastDir);
	QString path = displayPath();
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
		card->leaveEditMode();
		pathChanged(QDir::toNativeSeparators(relativePath));
	}
}

void VSTCardEditor::embedToggled(bool checked)
{
	initPlugin();

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
				refreshCard(tr("Plugin crashed when opening panel."));
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
		effect->startEditing(hwnd, &width, &height);
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
