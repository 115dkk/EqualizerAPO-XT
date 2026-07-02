/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "MultiConvolutionCardEditor.h"

#include <memory>

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
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
#include "filters/MultiConvolutionCommand.h"
#include "helpers/RegistryHelper.h"

MultiConvolutionCardEditor::MultiConvolutionCardEditor(FilterTable* filterTable, const QString& outputChannel, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable), path(path.trimmed())
{
	setObjectName(QStringLiteral("MultiConvolutionCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	view = SkinManager::instance()->createReferenceCardView(QStringLiteral("multiconvolution"), this);
	connect(view, SIGNAL(pathCommitted(QString)), this, SLOT(pathCommitted(QString)));
	layout->addWidget(view);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor actionColor(tokens.text);

	// Output channel the summed convolution is written to. An editable combo:
	// configureChannels() fills it with the channels that exist at this row so
	// the user picks the target ("which ear") from the real channels instead of
	// typing one, while a custom/virtual channel name can still be typed in.
	// It is part of the reference grammar ("<channel> <file>"), so it rides
	// inside the skin view as the leading widget.
	channelCombo = new QComboBox(view);
	channelCombo->setObjectName(QStringLiteral("MultiConvolutionCardChannel"));
	channelCombo->setEditable(true);
	channelCombo->setInsertPolicy(QComboBox::NoInsert);
	channelCombo->setToolTip(tr("Output channel the summed convolution is written to"));
	channelCombo->lineEdit()->setPlaceholderText(tr("Out ch"));
	channelCombo->setCurrentText(outputChannel.trimmed());
	channelCombo->setMaximumWidth(GUIHelper::scale(QSize(96, 0)).width());
	connect(channelCombo, SIGNAL(activated(int)), this, SIGNAL(updateModel()));
	connect(channelCombo->lineEdit(), SIGNAL(editingFinished()), this, SIGNAL(updateModel()));
	view->addLeadingWidget(channelCombo);

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

	// Let the active skin decorate this body. It shares the convolution row type
	// so a skin that styles convolution cards covers this one too.
	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("convolution");
	rowInfo.command = QStringLiteral("multiconvolution");
	SkinManager::instance()->prepareCommandRow(rowInfo, nullptr, nullptr, this);

	updateFileInfo();
}

void MultiConvolutionCardEditor::store(QString& command, QString& parameters)
{
	command = QStringLiteral("MultiConvolution");

	MultiConvolutionCommand cmd;
	cmd.outputChannel = channelCombo->currentText().trimmed().toStdWString();
	cmd.path = path.toStdWString();
	parameters = QString::fromStdWString(cmd.serialize());
}

void MultiConvolutionCardEditor::configureChannels(std::vector<std::wstring>& channelNames)
{
	// Repopulate the output options with the channels in scope at this row while
	// keeping whatever is currently entered/selected. The combo stays editable,
	// so a custom output channel that is not in the list survives as typed text.
	const QString current = channelCombo->currentText();
	const QSignalBlocker blocker(channelCombo);
	channelCombo->clear();
	for (const std::wstring& name : channelNames)
		channelCombo->addItem(QString::fromStdWString(name));
	channelCombo->setCurrentText(current);
}

void MultiConvolutionCardEditor::chooseFile()
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

void MultiConvolutionCardEditor::pathCommitted(const QString& text)
{
	path = text.trimmed();
	updateFileInfo();
	emit updateModel();
}

void MultiConvolutionCardEditor::importToConfig()
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

QString MultiConvolutionCardEditor::resolvedAbsolutePath() const
{
	if (filterTable == nullptr)
		return QString();

	return ConvolutionPathHelper::absolutePathForConfig(filterTable->getConfigPath(), path);
}

unsigned MultiConvolutionCardEditor::currentDeviceSampleRate() const
{
	if (filterTable == nullptr)
		return 0;

	std::shared_ptr<AbstractAPOInfo> device = filterTable->getSelectedDevice();
	return device != nullptr ? device->getSampleRate() : 0;
}

void MultiConvolutionCardEditor::updateFileInfo()
{
	ReferenceCardState state;
	state.kind = QStringLiteral("multiconvolution");
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
			if (asWritten.path() != QStringLiteral("."))
				state.directory = QDir::toNativeSeparators(asWritten.path());
		}
		else
		{
			state.fullPath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
			if (state.absolutePath)
				state.directory = QDir::toNativeSeparators(fileInfo.absolutePath());
			else if (asWritten.path() != QStringLiteral("."))
				state.directory = QDir::toNativeSeparators(asWritten.path());

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
				// The channel count matters here because it must match the number
				// of selected input channels.
				state.readout << tr("%1 ms").arg(QString::number(lengthMs, 'f', 1))
					<< tr("%1 samples").arg(static_cast<qlonglong>(sfInfo.frames))
					<< tr("%1 Hz").arg(sampleRate)
					<< tr("%1 ch").arg(sfInfo.channels);
				sf_close(file);

				const unsigned deviceRate = currentDeviceSampleRate();
				if (deviceRate != 0 && static_cast<unsigned>(sampleRate) != deviceRate)
				{
					state.statusText = tr("Sample rate does not match the device (%1 Hz)").arg(deviceRate);
					state.statusSeverity = ReferenceCardState::Severity::Warning;
				}
			}

			// See ConvolutionCardEditor: the audio service only holds rights
			// inside the config directory. The offscreen gallery skips the probe.
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
