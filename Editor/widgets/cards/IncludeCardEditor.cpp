/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The Include row's body: a reference card presenting the included
	configuration as a named entity (AR2, issue #97). The editor owns the
	behavior - path resolution, the file dialog, the jump into the included
	config, dependency import - and renders through the active skin's
	ReferenceCardView, so each skin answers the same reference in its own
	grammar. The path is edited inline through the view's shared edit mode.
*/

#include "IncludeCardEditor.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QToolButton>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportDialog.h"
#include "Editor/import/ImportExecutor.h"
#include "ReferenceCardView.h"
#include "helpers/RegistryHelper.h"

IncludeCardEditor::IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable), path(path.trimmed())
{
	setObjectName(QStringLiteral("IncludeCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	view = SkinManager::instance()->createReferenceCardView(QStringLiteral("include"), this);
	connect(view, SIGNAL(nameActivated()), this, SLOT(openFile()));
	connect(view, SIGNAL(pathCommitted(QString)), this, SLOT(pathCommitted(QString)));
	layout->addWidget(view);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor actionColor(tokens.text);

	chooseButton = new QToolButton(view);
	chooseButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	chooseButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), actionColor, 18));
	connect(chooseButton, SIGNAL(clicked()), this, SLOT(chooseFile()));
	view->addActionButton(ReferenceCardView::ActionRole::Browse, chooseButton);

	importButton = new QToolButton(view);
	importButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	importButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/import.svg"), actionColor, 18));
	importButton->setToolTip(tr("Copy this file and its dependencies into the config directory"));
	importButton->setVisible(false);
	connect(importButton, SIGNAL(clicked()), this, SLOT(importToConfig()));
	view->addActionButton(ReferenceCardView::ActionRole::Import, importButton);

	editButton = new QToolButton(view);
	editButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	editButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/pencil.svg"), actionColor, 18));
	editButton->setToolTip(tr("Edit the path as text"));
	connect(editButton, &QToolButton::clicked, view, &ReferenceCardView::enterEditMode);
	view->addActionButton(ReferenceCardView::ActionRole::EditPath, editButton);

	// Let the active skin decorate this Include body (the row is recreated on
	// skin switches, so construction is the only moment needed).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("include");
	rowInfo.command = QStringLiteral("include");
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	updateFileInfo();
}

void IncludeCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Include");
	parameters = path;
}

void IncludeCardEditor::chooseFile()
{
	if (filterTable == nullptr)
		return;

	QFileInfo fileInfo(filterTable->getConfigPath());
	QDir configDir = fileInfo.absoluteDir();
	if (!path.isEmpty())
		fileInfo = currentFileInfo();

	QFileDialog dialog(this, tr("Include file"), fileInfo.absolutePath(), QStringLiteral("*.txt"));
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("E-APO configurations (*.txt)"));
	if (!path.isEmpty())
		dialog.selectFile(fileInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		QString relativePath = configDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith(QStringLiteral("../../")))
			relativePath = absolutePath;
		path = QDir::toNativeSeparators(relativePath);
		updateFileInfo();
		emit updateModel();
	}
}

void IncludeCardEditor::openFile()
{
	if (filterTable == nullptr || path.isEmpty())
		return;

	filterTable->openConfig(currentFileInfo().absoluteFilePath());
}

void IncludeCardEditor::pathCommitted(const QString& text)
{
	path = text.trimmed();
	updateFileInfo();
	emit updateModel();
}

QFileInfo IncludeCardEditor::currentFileInfo() const
{
	if (filterTable == nullptr)
		return QFileInfo(path);

	QString normalizedPath = QDir::fromNativeSeparators(path);
	if (QDir::isAbsolutePath(normalizedPath))
		return QFileInfo(path);

	QFileInfo configInfo(filterTable->getConfigPath());
	QFileInfo fileInfo;
	fileInfo.setFile(configInfo.absoluteDir(), path);
	return fileInfo;
}

void IncludeCardEditor::updateFileInfo()
{
	ReferenceCardState state;
	state.kind = QStringLiteral("include");
	state.editText = path;

	bool offerImport = false;
	if (path.isEmpty())
	{
		state.missing = true;
		state.name = tr("No file selected");
	}
	else
	{
		const QString normalized = QDir::fromNativeSeparators(path);
		const QFileInfo asWritten(normalized);
		state.name = asWritten.fileName();
		state.absolutePath = QDir::isAbsolutePath(normalized);

		QFileInfo fileInfo = currentFileInfo();
		state.fullPath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
		// Location: the as-written parent for a relative reference (empty when
		// the file sits next to the config), the resolved directory when absolute.
		if (state.absolutePath)
			state.directory = QDir::toNativeSeparators(fileInfo.absolutePath());
		else if (asWritten.path() != QStringLiteral("."))
			state.directory = QDir::toNativeSeparators(asWritten.path());

		if (!fileInfo.exists())
		{
			state.missing = true;
		}
		else
		{
			state.nameClickable = true;

			// The audio service only holds rights it was granted; a file it
			// cannot read silently drops out of the config, so surface it and
			// offer the dependency import. The offscreen gallery renders
			// synthetic files with no meaningful ACL story - it skips the probe.
			if (!qEnvironmentVariableIsSet("EAPO_SKIN_GALLERY"))
			{
				const QString nativeAbsolute = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
				ACCESS_MASK mask = GENERIC_READ;
				try
				{
					mask = RegistryHelper::getFileAccessForUser(nativeAbsolute.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
				}
				catch (const RegistryException&)
				{
				}

				if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
				{
					state.statusText = tr("Not readable by the audio service");
					state.statusSeverity = ReferenceCardState::Severity::Critical;
					state.nameClickable = false;
					offerImport = true;
				}
			}
		}
	}

	// The Browse button doubles as the Locate recovery entry while the
	// reference is broken (AR2 X-4); the label is the affordance.
	const bool locate = state.missing && !path.isEmpty();
	chooseButton->setText(locate ? tr("Locate...") : QString());
	chooseButton->setToolTip(locate ? tr("Locate the missing file") : tr("Choose include file"));

	view->setState(state);
	importButton->setVisible(offerImport);
}

void IncludeCardEditor::importToConfig()
{
	if (filterTable == nullptr)
		return;

	QFileInfo fileInfo = currentFileInfo();
	if (!fileInfo.exists())
		return;

	QString sourcePath = fileInfo.absoluteFilePath();
	QString configDir = QFileInfo(filterTable->getConfigPath()).absolutePath();

	auto manifest = EqAPO::Import::ConfigDependencyScanner::scan(sourcePath, configDir);
	if (manifest.items.isEmpty())
	{
		QMessageBox::warning(this, tr("Import"), tr("Nothing to import: %1").arg(manifest.warnings.join('\n')));
		return;
	}

	EqAPO::Import::ImportDialog dialog(manifest, configDir, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	auto result = EqAPO::Import::ImportExecutor::execute(manifest, configDir);
	if (!result.success)
	{
		QMessageBox::warning(this, tr("Import"),
			tr("Some files could not be copied:\n%1").arg(result.errors.join('\n')));
		return;
	}

	path = QDir::toNativeSeparators(manifest.rootDest);
	updateFileInfo();
	emit updateModel();
}
