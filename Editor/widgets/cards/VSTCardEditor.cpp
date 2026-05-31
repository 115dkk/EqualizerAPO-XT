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
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "helpers/aeffectx.h"
#include "helpers/StringHelper.h"
#include "helpers/RegistryHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/MainWindow.h"
#include "Editor/guis/VSTPluginFilterGUIDialog.h"

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

	QHBoxLayout* row = new QHBoxLayout();
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(8);

	QLabel* glyph = new QLabel(this);
	glyph->setObjectName(QStringLiteral("VSTCardGlyph"));
	glyph->setPixmap(style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(20, 20));
	row->addWidget(glyph, 0, Qt::AlignVCenter);

	pathEdit = new QLineEdit(this);
	pathEdit->setObjectName(QStringLiteral("VSTCardPath"));
	pathEdit->setPlaceholderText(tr("VST plugin (.dll)"));
	connect(pathEdit, SIGNAL(editingFinished()), this, SLOT(pathEditingFinished()));
	row->addWidget(pathEdit, 1);

	selectButton = new QToolButton(this);
	selectButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	selectButton->setIcon(QIcon(QStringLiteral(":/icons/document-open.ico")));
	selectButton->setToolTip(tr("Select VST plugin"));
	connect(selectButton, SIGNAL(clicked()), this, SLOT(selectFile()));
	row->addWidget(selectButton);

	openPanelButton = new QPushButton(tr("Open panel"), this);
	openPanelButton->setObjectName(QStringLiteral("VSTCardPanelButton"));
	connect(openPanelButton, SIGNAL(clicked()), this, SLOT(openPanel()));
	row->addWidget(openPanelButton);

	optionsButton = new QToolButton(this);
	optionsButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	optionsButton->setText(QStringLiteral("..."));
	optionsButton->setPopupMode(QToolButton::InstantPopup);
	QMenu* menu = new QMenu(optionsButton);
	embedAction = menu->addAction(tr("Embed panel in card"));
	embedAction->setCheckable(true);
	connect(embedAction, SIGNAL(toggled(bool)), this, SLOT(embedToggled(bool)));
	optionsButton->setMenu(menu);
	row->addWidget(optionsButton);

	root->addLayout(row);

	statusLabel = new QLabel(this);
	statusLabel->setObjectName(QStringLiteral("VSTCardStatus"));
	root->addWidget(statusLabel);

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

	QString absolutePath = QString::fromStdWString(library->getLibPath());
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));
	QString relativePath = QDir::toNativeSeparators(pluginsDir.relativeFilePath(absolutePath));
	if (relativePath.startsWith(QDir::toNativeSeparators("../../")))
		relativePath = absolutePath;
	pathEdit->setText(relativePath);

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
			effect = new VSTPluginInstance(library, 1);
			if (effect->initialize())
			{
				effect->setLanguage(QLocale().language() == QLocale::German ? 2 : 1);
				effect->setAutomateFunc([this]() { onAutomate(); });

				color = Qt::black;
				text = QString::fromStdWString(effect->getName());
			}
			else
			{
				delete effect;
				effect = nullptr;
				color = Qt::red;
				text = tr("Plugin crashed during initialization.");
			}
		}
	}

	QPalette palette = statusLabel->palette();
	palette.setColor(QPalette::Active, QPalette::WindowText, color);
	palette.setColor(QPalette::Inactive, QPalette::WindowText, color);
	statusLabel->setPalette(palette);
	statusLabel->setText(text);
}

void VSTCardEditor::pathEditingFinished()
{
	if (QString::fromStdWString(library->getLibPath()) != pathEdit->text())
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
		QString path = pathEdit->text();
		if (path.length() > 0)
			path = QDir::toNativeSeparators(QFileInfo(pluginsDir, pathEdit->text()).absoluteFilePath());
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
}

void VSTCardEditor::selectFile()
{
	QDir pluginsDir(QString::fromStdWString(VSTPluginLibrary::getDefaultPluginPath()));

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QString lastDir = settings.value("vst/lastDir", "").toString();
	if (lastDir == "")
		lastDir = pluginsDir.absolutePath();

	QFileInfo fileInfo(lastDir);
	QString path = pathEdit->text();
	if (path.length() > 0)
		fileInfo.setFile(pluginsDir, path);

	QFileDialog dialog(this, tr("Select VST plugin"), fileInfo.absoluteFilePath(), "*.dll");
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("VST plugins (*.dll)"));
	if (path.length() > 0)
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		settings.setValue("vst/lastDir", QDir::toNativeSeparators(QFileInfo(absolutePath).absolutePath()));
		QString relativePath = pluginsDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith("../../"))
			relativePath = absolutePath;
		pathEdit->setText(QDir::toNativeSeparators(relativePath));
		pathEditingFinished();
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
		statusLabel->setVisible(!enable);

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
				statusLabel->setVisible(true);

				QPalette palette = statusLabel->palette();
				palette.setColor(QPalette::Active, QPalette::WindowText, Qt::red);
				palette.setColor(QPalette::Inactive, QPalette::WindowText, Qt::red);
				statusLabel->setPalette(palette);
				statusLabel->setText(tr("Plugin crashed when opening panel."));
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
