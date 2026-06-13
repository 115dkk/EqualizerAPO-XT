#include "IncludeCardEditor.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTextStream>
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
#include "Editor/widgets/cards/ReferenceCard.h"
#include "helpers/RegistryHelper.h"

IncludeCardEditor::IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable), includePath(path.trimmed())
{
	setObjectName(QStringLiteral("IncludeCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	card = new ReferenceCard(QStringLiteral("include"), this);
	connect(card, &ReferenceCard::nameActivated, this, &IncludeCardEditor::openFile);
	connect(card, &ReferenceCard::editRequested, this, [this]() { card->enterEditMode(); });
	connect(card, &ReferenceCard::pathEdited, this, &IncludeCardEditor::pathChanged);
	layout->addWidget(card, 1);

	const QColor actionColor(SkinManager::instance()->tokens().text);

	// X-4: a recovery entry point that replaces the bare "File not found"
	// caption. Visible only while the reference is broken.
	locateButton = card->addActionButton(QStringLiteral("RefLocateAction"));
	locateButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), actionColor, 18));
	locateButton->setText(tr("Locate..."));
	locateButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	locateButton->setToolTip(tr("Locate the missing include file"));
	locateButton->setVisible(false);
	connect(locateButton, &QToolButton::clicked, this, &IncludeCardEditor::locateFile);

	importButton = card->addActionButton(QStringLiteral("IncludeRefImport"));
	importButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/import.svg"), actionColor, 18));
	importButton->setToolTip(tr("Copy this file and its dependencies into the config directory"));
	importButton->setVisible(false);
	connect(importButton, &QToolButton::clicked, this, &IncludeCardEditor::importToConfig);

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
	parameters = includePath;
}

void IncludeCardEditor::locateFile()
{
	if (filterTable == nullptr)
		return;

	QFileInfo configInfo(filterTable->getConfigPath());
	QDir configDir = configInfo.absoluteDir();
	// Scope the dialog to the last valid directory, falling back to the config
	// directory (X-4).
	QString startDir = configDir.absolutePath();
	if (!includePath.isEmpty())
	{
		QFileInfo current = currentFileInfo();
		if (!current.absolutePath().isEmpty())
			startDir = current.absolutePath();
	}

	QFileDialog dialog(this, tr("Include file"), startDir, QStringLiteral("*.txt"));
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("E-APO configurations (*.txt)"));
	if (!includePath.isEmpty())
		dialog.selectFile(currentFileInfo().fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		QString absolutePath = dialog.selectedFiles().first();
		QString relativePath = configDir.relativeFilePath(absolutePath);
		if (relativePath.startsWith(QStringLiteral("../../")))
			relativePath = absolutePath;
		includePath = QDir::toNativeSeparators(relativePath);
		card->leaveEditMode();
		updateFileInfo();
		emit updateModel();
	}
}

void IncludeCardEditor::openFile()
{
	if (filterTable == nullptr || includePath.isEmpty())
		return;
	if (!currentFileInfo().exists())
		return;

	filterTable->openConfig(currentFileInfo().absoluteFilePath());
}

void IncludeCardEditor::pathChanged(const QString& newPath)
{
	const QString trimmed = newPath.trimmed();
	if (trimmed == includePath)
	{
		updateFileInfo();
		return;
	}
	includePath = trimmed;
	updateFileInfo();
	emit updateModel();
}

QFileInfo IncludeCardEditor::currentFileInfo() const
{
	return currentFileInfo(includePath);
}

QFileInfo IncludeCardEditor::currentFileInfo(const QString& path) const
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

QString IncludeCardEditor::previewText() const
{
	QFileInfo info = currentFileInfo();
	if (!info.exists())
		return QString();

	QFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();

	QTextStream stream(&file);
	QStringList lines;
	for (int i = 0; i < 5 && !stream.atEnd(); i++)
		lines.append(stream.readLine());
	if (lines.isEmpty())
		return QString();
	return lines.join(QLatin1Char('\n'));
}

void IncludeCardEditor::updateFileInfo()
{
	ReferenceCardState state;
	const QString normalizedPath = QDir::fromNativeSeparators(includePath);
	state.absolutePath = QDir::isAbsolutePath(normalizedPath);

	bool offerImport = false;
	canOpen = false;

	if (includePath.isEmpty())
	{
		state.name = tr("No file selected");
		state.directory.clear();
		state.fullPath.clear();
		state.missing = true;
		state.statusText = tr("No include file selected");
		state.statusSeverity = ReferenceCardState::Severity::Warning;
	}
	else
	{
		QFileInfo fileInfo = currentFileInfo();
		state.name = fileInfo.fileName();
		state.directory = QDir::toNativeSeparators(fileInfo.absolutePath());
		state.fullPath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());

		if (!fileInfo.exists())
		{
			state.missing = true;
			state.statusText = tr("File not found - use Locate to reconnect");
			state.statusSeverity = ReferenceCardState::Severity::Critical;
		}
		else
		{
			QString resolvedPath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
			canOpen = true;
			state.nameClickable = true;
			state.nameTooltip = previewText();

			if (filterTable != nullptr && state.absolutePath)
			{
				QString configPath = QDir::cleanPath(QFileInfo(filterTable->getConfigPath()).absolutePath());
				if (!QDir::cleanPath(fileInfo.absolutePath()).startsWith(configPath, Qt::CaseInsensitive))
				{
					state.statusText = tr("External absolute include path");
					state.statusSeverity = ReferenceCardState::Severity::Warning;
				}
			}

			ACCESS_MASK mask = GENERIC_READ;
			try
			{
				mask = RegistryHelper::getFileAccessForUser(resolvedPath.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
			}
			catch (const RegistryException&)
			{
			}

			if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
			{
				state.statusText = tr("Not readable by the audio service - use the import button");
				state.statusSeverity = ReferenceCardState::Severity::Critical;
				offerImport = true;
			}
		}
	}

	card->setState(state);
	locateButton->setVisible(state.missing);
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

	includePath = QDir::toNativeSeparators(manifest.rootDest);
	updateFileInfo();
	emit updateModel();
}
