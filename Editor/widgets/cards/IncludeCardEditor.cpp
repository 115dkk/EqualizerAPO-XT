/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	The Include row's body: a reference card presenting the included
	configuration as a named entity. The editor owns the
	behavior - path resolution, the file dialog, the jump into the included
	config, dependency import - and renders through the active skin's
	ReferenceCardView, so each skin answers the same reference in its own
	grammar. The pencil edits the *file* (it opens the included config in the
	editor, same as clicking the name); the reference itself is changed
	through Browse/Locate. Inline path-text editing is intentionally not
	offered here - an edit affordance that rewrote the raw config line
	instead of opening the file contradicted the card's own grammar.
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
#include "FileReferenceController.h"
#include "services/registry/WindowsRegistry.h"

IncludeCardEditor::IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable),
	  reference(new FileReferenceController(QStringLiteral("include"), path, this))
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
	editButton->setToolTip(tr("Edit the included file in the editor"));
	connect(editButton, SIGNAL(clicked()), this, SLOT(openFile()));
	view->addActionButton(ReferenceCardView::ActionRole::OpenTarget, editButton);

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
	parameters = reference->writtenPath();
}

void IncludeCardEditor::chooseFile()
{
	if (filterTable == nullptr)
		return;

	QFileInfo fileInfo(filterTable->getConfigPath());
	QDir configDir = fileInfo.absoluteDir();
	if (!reference->writtenPath().isEmpty())
		fileInfo = currentFileInfo();

	const QString selected = reference->chooseExistingFile(
		this, tr("Include file"), fileInfo.absolutePath(),
		tr("E-APO configurations (*.txt)"), configDir.absolutePath(),
		reference->writtenPath().isEmpty() ? QString() : fileInfo.fileName());
	if (!selected.isEmpty())
	{
		updateFileInfo();
		emit updateModel();
	}
}

void IncludeCardEditor::openFile()
{
	if (filterTable == nullptr || reference->writtenPath().isEmpty())
		return;

	filterTable->openConfig(currentFileInfo().absoluteFilePath());
}

void IncludeCardEditor::pathCommitted(const QString& text)
{
	reference->setWrittenPath(text);
	updateFileInfo();
	emit updateModel();
}

QFileInfo IncludeCardEditor::currentFileInfo() const
{
	if (filterTable == nullptr)
		return QFileInfo(reference->writtenPath());

	QString normalizedPath = QDir::fromNativeSeparators(reference->writtenPath());
	if (QDir::isAbsolutePath(normalizedPath))
		return QFileInfo(reference->writtenPath());

	QFileInfo configInfo(filterTable->getConfigPath());
	QFileInfo fileInfo;
	fileInfo.setFile(configInfo.absoluteDir(), reference->writtenPath());
	return fileInfo;
}

void IncludeCardEditor::updateFileInfo()
{
	const QFileInfo fileInfo = currentFileInfo();
	reference->setResolvedPath(fileInfo.absoluteFilePath());
	ReferenceCardState state = reference->describe(tr("No file selected"));
	bool offerImport = false;
	if (!state.missing)
	{
		state.nameClickable = true;
		if (!FileReferenceController::isReadableByAudioService(state.fullPath))
		{
			state.statusText = tr("Not readable by the audio service");
			state.statusSeverity = ReferenceCardState::Severity::Critical;
			state.nameClickable = false;
			offerImport = true;
		}
	}

	// The Browse button doubles as the Locate recovery entry while the
	// reference is broken; the label is the affordance.
	const bool locate = state.missing && !reference->writtenPath().isEmpty();
	chooseButton->setText(locate ? tr("Locate...") : QString());
	chooseButton->setToolTip(locate ? tr("Locate the missing file") : tr("Choose include file"));

	// Opening a reference that does not resolve would only produce an error;
	// while broken, recovery (Locate) is the offered action instead.
	editButton->setEnabled(!state.missing && !reference->writtenPath().isEmpty());

	view->setState(state);
	importButton->setVisible(offerImport);
}

void IncludeCardEditor::importToConfig()
{
	if (filterTable == nullptr)
		return;

	reference->setResolvedPath(currentFileInfo().absoluteFilePath());
	if (!reference->importIntoConfig(this, filterTable->getConfigPath()))
		return;
	updateFileInfo();
	emit updateModel();
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(Include, [](FilterTable* filterTable, const QString&, const QString& parameters) -> IFilterGUI* {
	return new IncludeCardEditor(filterTable, parameters);
})
