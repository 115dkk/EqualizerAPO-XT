#include "IncludeCardEditor.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Editor/FilterTable.h"
#include "helpers/RegistryHelper.h"

IncludeCardEditor::IncludeCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable)
{
	setObjectName(QStringLiteral("IncludeCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	QLabel* fileGlyph = new QLabel(this);
	fileGlyph->setObjectName(QStringLiteral("IncludeCardGlyph"));
	fileGlyph->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(20, 20));
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
	chooseButton->setIcon(QIcon(QStringLiteral(":/icons/document-open.ico")));
	chooseButton->setToolTip(tr("Choose include file"));
	connect(chooseButton, SIGNAL(clicked()), this, SLOT(chooseFile()));
	layout->addWidget(chooseButton, 0, Qt::AlignTop);

	openButton = new QToolButton(this);
	openButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	openButton->setIcon(QIcon(QStringLiteral(":/icons/accessories-text-editor.ico")));
	openButton->setToolTip(tr("Open include file"));
	connect(openButton, SIGNAL(clicked()), this, SLOT(openFile()));
	layout->addWidget(openButton, 0, Qt::AlignTop);

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
		fileInfo.setFile(configDir, path);

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

	QFileInfo configInfo(filterTable->getConfigPath());
	QFileInfo fileInfo;
	fileInfo.setFile(configInfo.absoluteDir(), pathEdit->text());
	return fileInfo;
}

void IncludeCardEditor::updateFileInfo()
{
	QString error;
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

			ACCESS_MASK mask = GENERIC_READ;
			try
			{
				mask = RegistryHelper::getFileAccessForUser(path.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
			}
			catch (const RegistryException&)
			{
			}

			if ((mask & GENERIC_READ) != GENERIC_READ && (mask & FILE_GENERIC_READ) != FILE_GENERIC_READ)
				error = tr("The file is not readable for the audio service.\nChange the file permissions or copy the file to the config directory.");
		}
	}

	statusLabel->setVisible(!error.isEmpty());
	statusLabel->setText(error);
	openButton->setEnabled(error.isEmpty() && !pathEdit->text().isEmpty());
}
