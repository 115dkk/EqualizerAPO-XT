#include "IncludeCardEditor.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportDialog.h"
#include "Editor/import/ImportExecutor.h"
#include "helpers/RegistryHelper.h"

IncludeCardEditor::IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable)
{
	setObjectName(QStringLiteral("IncludeCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor glyphColor(tokens.mutedText);
	const QColor actionColor(tokens.text);

	QLabel* fileGlyph = new QLabel(this);
	fileGlyph->setObjectName(QStringLiteral("IncludeCardGlyph"));
	fileGlyph->setPixmap(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/file-include.svg"), glyphColor, 20).pixmap(GUIHelper::scale(QSize(20, 20))));
	layout->addWidget(fileGlyph, 0, Qt::AlignVCenter);

	QWidget* pathBlock = new QWidget(this);
	QVBoxLayout* pathLayout = new QVBoxLayout(pathBlock);
	pathLayout->setContentsMargins(0, 0, 0, 0);
	pathLayout->setSpacing(5);

	pathEdit = new QLineEdit(pathBlock);
	pathEdit->setObjectName(QStringLiteral("IncludeCardPath"));
	pathEdit->setText(path.trimmed());
	pathEdit->setPlaceholderText(tr("Configuration file"));
	connect(pathEdit, SIGNAL(editingFinished()), this, SLOT(pathEdited()));
	pathLayout->addWidget(pathEdit);

	statusLabel = new QLabel(pathBlock);
	statusLabel->setObjectName(QStringLiteral("IncludeCardStatus"));
	statusLabel->setWordWrap(true);
	pathLayout->addWidget(statusLabel);

	layout->addWidget(pathBlock, 1);

	chooseButton = new QToolButton(this);
	chooseButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	chooseButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), actionColor, 18));
	chooseButton->setToolTip(tr("Choose include file"));
	connect(chooseButton, SIGNAL(clicked()), this, SLOT(chooseFile()));
	layout->addWidget(chooseButton, 0, Qt::AlignTop);

	openButton = new QToolButton(this);
	openButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	openButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/pencil.svg"), actionColor, 18));
	openButton->setToolTip(tr("Open include file"));
	connect(openButton, SIGNAL(clicked()), this, SLOT(openFile()));
	layout->addWidget(openButton, 0, Qt::AlignTop);

	importButton = new QToolButton(this);
	importButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	importButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/import.svg"), actionColor, 18));
	importButton->setToolTip(tr("Copy this file and its dependencies into the config directory"));
	importButton->setVisible(false);
	connect(importButton, SIGNAL(clicked()), this, SLOT(importToConfig()));
	layout->addWidget(importButton, 0, Qt::AlignTop);

	updateFileInfo();
}

void IncludeCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Include");
	parameters = pathEdit->text();
}

void IncludeCardEditor::chooseFile()
{
	if (filterTable == nullptr)
		return;

	QFileInfo fileInfo(filterTable->getConfigPath());
	QDir configDir = fileInfo.absoluteDir();
	QString path = pathEdit->text();
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
		pathEdit->setText(QDir::toNativeSeparators(relativePath));
		updateFileInfo();
		emit updateModel();
	}
}

void IncludeCardEditor::openFile()
{
	if (filterTable == nullptr)
		return;

	QString path = pathEdit->text();
	if (path.isEmpty())
		return;

	filterTable->openConfig(currentFileInfo().absoluteFilePath());
}

void IncludeCardEditor::pathEdited()
{
	updateFileInfo();
	emit updateModel();
}

QFileInfo IncludeCardEditor::currentFileInfo() const
{
	if (filterTable == nullptr)
		return QFileInfo(pathEdit->text());

	QString path = pathEdit->text();
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
	QString error;
	QString warning;
	bool offerImport = false;
	QString path = pathEdit->text();
	if (path.isEmpty())
	{
		error = tr("No file selected");
	}
	else
	{
		QFileInfo fileInfo = currentFileInfo();
		if (!fileInfo.exists())
		{
			error = tr("File not found");
		}
		else
		{
			path = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
			if (filterTable != nullptr)
			{
				QString normalizedPath = QDir::cleanPath(QDir::fromNativeSeparators(pathEdit->text()));
				QString configPath = QDir::cleanPath(QFileInfo(filterTable->getConfigPath()).absolutePath());
				if (QDir::isAbsolutePath(normalizedPath) && !QDir::cleanPath(fileInfo.absolutePath()).startsWith(configPath, Qt::CaseInsensitive))
					warning = tr("External absolute include path");
			}

			ACCESS_MASK mask = GENERIC_READ;
			try
			{
				mask = RegistryHelper::getFileAccessForUser(path.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
			}
			catch (const RegistryException&)
			{
			}

			if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
			{
				error = tr("The file is not readable for the audio service.\nClick the import button to copy it into the config directory.");
				offerImport = true;
			}
		}
	}

	statusLabel->setVisible(!error.isEmpty() || !warning.isEmpty());
	statusLabel->setText(error.isEmpty() ? warning : error);
	statusLabel->setProperty("severity", error.isEmpty() ? QStringLiteral("warning") : QStringLiteral("critical"));
	statusLabel->style()->unpolish(statusLabel);
	statusLabel->style()->polish(statusLabel);
	openButton->setEnabled(error.isEmpty() && !pathEdit->text().isEmpty());
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

	pathEdit->setText(QDir::toNativeSeparators(manifest.rootDest));
	updateFileInfo();
	emit updateModel();
}
