#include "ConvolutionCardEditor.h"

#include <memory>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QToolButton>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#include <sndfile.h>

#include "AbstractAPOInfo.h"
#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/helpers/ConvolutionPathHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportDialog.h"
#include "Editor/import/ImportExecutor.h"
#include "ReferenceCardView.h"
#include "filters/ConvolutionCommand.h"
#include "helpers/RegistryHelper.h"

ConvolutionCardEditor::ConvolutionCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable), path(path.trimmed())
{
	setObjectName(QStringLiteral("ConvolutionCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	view = SkinManager::instance()->createReferenceCardView(QStringLiteral("convolution"), this);
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
	importButton->setToolTip(tr("Copy this file into the config directory"));
	importButton->setVisible(false);
	connect(importButton, SIGNAL(clicked()), this, SLOT(importToConfig()));
	view->addActionButton(ReferenceCardView::ActionRole::Import, importButton);

	editButton = new QToolButton(view);
	editButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	editButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/pencil.svg"), actionColor, 18));
	editButton->setToolTip(tr("Edit the path as text"));
	connect(editButton, &QToolButton::clicked, view, &ReferenceCardView::enterEditMode);
	view->addActionButton(ReferenceCardView::ActionRole::EditPath, editButton);

	// Let the active skin decorate this convolution body (the row is recreated on
	// skin switches, so construction is the only moment needed).
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("convolution");
	rowInfo.command = QStringLiteral("convolution");
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	updateFileInfo();
}

void ConvolutionCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("Convolution");

	ConvolutionCommand cmd;
	cmd.path = path.toStdWString();
	parameters = QString::fromStdWString(cmd.serialize());
}

void ConvolutionCardEditor::chooseFile()
{
	if (filterTable == nullptr)
		return;

	const QString configPath = filterTable->getConfigPath();
	const QString currentAbsolute = ConvolutionPathHelper::absolutePathForConfig(configPath, path);
	QFileInfo startInfo(currentAbsolute.isEmpty() ? configPath : currentAbsolute);

	QFileDialog dialog(this, tr("Select impulse response file"), startInfo.absolutePath(), QStringLiteral("*.wav *.flac *.ogg"));
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("Impulse response (*.wav *.flac *.ogg)"));
	if (!path.isEmpty())
		dialog.selectFile(startInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		const QString selected = dialog.selectedFiles().first();
		path = ConvolutionPathHelper::displayPathForSelection(configPath, selected);
		updateFileInfo();
		emit updateModel();
	}
}

void ConvolutionCardEditor::pathCommitted(const QString& text)
{
	path = text.trimmed();
	updateFileInfo();
	emit updateModel();
}

void ConvolutionCardEditor::importToConfig()
{
	if (filterTable == nullptr)
		return;

	const QString source = resolvedAbsolutePath();
	if (source.isEmpty() || !QFileInfo::exists(source))
		return;

	const QString configDir = QFileInfo(filterTable->getConfigPath()).absolutePath();

	EqAPO::Import::ImportManifest manifest = EqAPO::Import::ConfigDependencyScanner::scan(source, configDir);
	if (manifest.items.isEmpty())
	{
		QMessageBox::warning(this, tr("Import"), tr("Nothing to import: %1").arg(manifest.warnings.join('\n')));
		return;
	}

	EqAPO::Import::ImportDialog dialog(manifest, configDir, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	EqAPO::Import::ExecutionResult result = EqAPO::Import::ImportExecutor::execute(manifest, configDir);
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

QString ConvolutionCardEditor::resolvedAbsolutePath() const
{
	if (filterTable == nullptr)
		return QString();

	return ConvolutionPathHelper::absolutePathForConfig(filterTable->getConfigPath(), path);
}

unsigned ConvolutionCardEditor::currentDeviceSampleRate() const
{
	if (filterTable == nullptr)
		return 0;

	std::shared_ptr<AbstractAPOInfo> device = filterTable->getSelectedDevice();
	return device != nullptr ? device->getSampleRate() : 0;
}

void ConvolutionCardEditor::updateFileInfo()
{
	ReferenceCardState state;
	state.kind = QStringLiteral("convolution");
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

		const QString absolute = resolvedAbsolutePath();
		QFileInfo fileInfo(absolute);
		if (absolute.isEmpty() || !fileInfo.exists())
		{
			state.missing = true;
			if (state.absolutePath)
				state.directory = QDir::toNativeSeparators(asWritten.path());
			else if (asWritten.path() != QStringLiteral("."))
				state.directory = QDir::toNativeSeparators(asWritten.path());
		}
		else
		{
			state.fullPath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
			if (state.absolutePath)
				state.directory = QDir::toNativeSeparators(fileInfo.absolutePath());
			else if (asWritten.path() != QStringLiteral("."))
				state.directory = QDir::toNativeSeparators(asWritten.path());

			// Impulse-response readout. The Editor runs as the interactive user,
			// so it usually reads the file even when the audio service cannot;
			// the readout stays visible next to any permission status below.
			SF_INFO sfInfo = {};
			SNDFILE* file = sf_wchar_open(state.fullPath.toStdWString().c_str(), SFM_READ, &sfInfo);
			if (file == nullptr)
			{
				state.statusText = tr("Unsupported file format");
				state.statusSeverity = ReferenceCardState::Severity::Critical;
			}
			else
			{
				const int sampleRate = sfInfo.samplerate;
				const double lengthMs = sampleRate > 0 ? sfInfo.frames * 1000.0 / sampleRate : 0.0;
				state.readout << tr("%1 ms").arg(QString::number(lengthMs, 'f', 1))
					<< tr("%1 samples").arg(static_cast<qlonglong>(sfInfo.frames))
					<< tr("%1 Hz").arg(sampleRate);
				sf_close(file);

				const unsigned deviceRate = currentDeviceSampleRate();
				if (deviceRate != 0 && static_cast<unsigned>(sampleRate) != deviceRate)
				{
					state.statusText = tr("Sample rate does not match the device (%1 Hz)").arg(deviceRate);
					state.statusSeverity = ReferenceCardState::Severity::Warning;
				}
			}

			// The audio service only holds rights inside the config directory, so
			// a file it cannot read is offered for import; a readable file that
			// merely lives elsewhere is offered too, since copying it in keeps the
			// config self-contained. The offscreen gallery renders synthetic
			// files with no meaningful ACL story - it skips the probe.
			if (!qEnvironmentVariableIsSet("EAPO_SKIN_GALLERY"))
			{
				ACCESS_MASK mask = GENERIC_READ;
				try
				{
					mask = RegistryHelper::getFileAccessForUser(state.fullPath.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
				}
				catch (const RegistryException&)
				{
				}
				const bool readableByService = (mask & GENERIC_READ) == GENERIC_READ || (mask & FILE_GENERIC_READ) == FILE_GENERIC_READ;

				if (!readableByService)
				{
					state.statusText = tr("Not readable by the audio service");
					state.statusSeverity = ReferenceCardState::Severity::Critical;
					offerImport = true;
				}
				else
				{
					const QString configDir = QDir::cleanPath(QFileInfo(filterTable->getConfigPath()).absolutePath());
					const QString fileDir = QDir::cleanPath(fileInfo.absolutePath());
					if (!configDir.isEmpty() && !fileDir.startsWith(configDir, Qt::CaseInsensitive))
						offerImport = true;
				}
			}
		}
	}

	const bool locate = state.missing && !path.isEmpty();
	chooseButton->setText(locate ? tr("Locate...") : QString());
	chooseButton->setToolTip(locate ? tr("Locate the missing file") : tr("Select impulse response file"));

	view->setState(state);
	importButton->setVisible(offerImport);
}
