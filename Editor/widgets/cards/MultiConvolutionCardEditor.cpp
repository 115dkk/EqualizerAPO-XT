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

#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

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
#include "filters/MultiConvolutionCommand.h"
#include "helpers/RegistryHelper.h"

MultiConvolutionCardEditor::MultiConvolutionCardEditor(FilterTable* filterTable, const QString& outputChannel, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable)
{
	setObjectName(QStringLiteral("MultiConvolutionCardEditor"));

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor glyphColor(tokens.mutedText);
	const QColor actionColor(tokens.text);

	QLabel* fileGlyph = new QLabel(this);
	// Reuse the Include glyph identity so every skin's transparent-glyph rule
	// already applies; the artwork is the same impulse-response trace the
	// single-input convolution card uses.
	fileGlyph->setObjectName(QStringLiteral("IncludeCardGlyph"));
	fileGlyph->setPixmap(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/waveform.svg"), glyphColor, 20).pixmap(GUIHelper::scale(QSize(20, 20))));
	layout->addWidget(fileGlyph, 0, Qt::AlignVCenter);

	// Output channel that the summed convolution is written to. Kept narrow: a
	// channel name is a short token (L, R, or a custom upper-case name).
	channelEdit = new QLineEdit(this);
	channelEdit->setObjectName(QStringLiteral("MultiConvolutionCardChannel"));
	channelEdit->setText(outputChannel.trimmed());
	channelEdit->setPlaceholderText(tr("Out ch"));
	channelEdit->setToolTip(tr("Output channel the summed convolution is written to"));
	channelEdit->setMaximumWidth(GUIHelper::scale(QSize(72, 0)).width());
	connect(channelEdit, SIGNAL(editingFinished()), this, SIGNAL(updateModel()));
	layout->addWidget(channelEdit, 0, Qt::AlignTop);

	QWidget* pathBlock = new QWidget(this);
	QVBoxLayout* pathLayout = new QVBoxLayout(pathBlock);
	pathLayout->setContentsMargins(0, 0, 0, 0);
	pathLayout->setSpacing(5);

	pathEdit = new QLineEdit(pathBlock);
	pathEdit->setObjectName(QStringLiteral("IncludeCardPath"));
	pathEdit->setText(path.trimmed());
	pathEdit->setPlaceholderText(tr("Multi-channel impulse response file"));
	connect(pathEdit, SIGNAL(editingFinished()), this, SLOT(pathEdited()));
	pathLayout->addWidget(pathEdit);

	infoLabel = new QLabel(pathBlock);
	infoLabel->setObjectName(QStringLiteral("ConvolutionCardInfo"));
	infoLabel->setStyleSheet(QStringLiteral("color: %1;").arg(glyphColor.name()));
	pathLayout->addWidget(infoLabel);

	statusLabel = new QLabel(pathBlock);
	statusLabel->setObjectName(QStringLiteral("IncludeCardStatus"));
	statusLabel->setWordWrap(true);
	pathLayout->addWidget(statusLabel);

	layout->addWidget(pathBlock, 1);

	chooseButton = new QToolButton(this);
	chooseButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	chooseButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/folder-open.svg"), actionColor, 18));
	chooseButton->setToolTip(tr("Select impulse response file"));
	connect(chooseButton, SIGNAL(clicked()), this, SLOT(chooseFile()));
	layout->addWidget(chooseButton, 0, Qt::AlignTop);

	importButton = new QToolButton(this);
	importButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	importButton->setIcon(GUIHelper::tintedIcon(QStringLiteral(":/icons/modern/import.svg"), actionColor, 18));
	importButton->setToolTip(tr("Copy this file into the config directory"));
	importButton->setVisible(false);
	connect(importButton, SIGNAL(clicked()), this, SLOT(importToConfig()));
	layout->addWidget(importButton, 0, Qt::AlignTop);

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
	cmd.outputChannel = channelEdit->text().trimmed().toStdWString();
	cmd.path = pathEdit->text().toStdWString();
	parameters = QString::fromStdWString(cmd.serialize());
}

void MultiConvolutionCardEditor::chooseFile()
{
	if (filterTable == nullptr)
		return;

	const QString configPath = filterTable->getConfigPath();
	const QString current = pathEdit->text();
	const QString currentAbsolute = ConvolutionPathHelper::absolutePathForConfig(configPath, current);
	QFileInfo startInfo(currentAbsolute.isEmpty() ? configPath : currentAbsolute);

	QFileDialog dialog(this, tr("Select impulse response file"), startInfo.absolutePath(), QStringLiteral("*.wav *.flac *.ogg"));
	dialog.setFileMode(QFileDialog::ExistingFile);
	dialog.setNameFilter(tr("Impulse response (*.wav *.flac *.ogg)"));
	if (!current.isEmpty())
		dialog.selectFile(startInfo.fileName());
	if (dialog.exec() == QDialog::Accepted)
	{
		const QString selected = dialog.selectedFiles().first();
		pathEdit->setText(ConvolutionPathHelper::displayPathForSelection(configPath, selected));
		updateFileInfo();
		emit updateModel();
	}
}

void MultiConvolutionCardEditor::pathEdited()
{
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

	pathEdit->setText(QDir::toNativeSeparators(manifest.rootDest));
	updateFileInfo();
	emit updateModel();
}

QString MultiConvolutionCardEditor::resolvedAbsolutePath() const
{
	if (filterTable == nullptr)
		return QString();

	return ConvolutionPathHelper::absolutePathForConfig(filterTable->getConfigPath(), pathEdit->text());
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
	QString info;
	QString error;
	QString warning;
	bool offerImport = false;

	const QString path = pathEdit->text();
	if (path.isEmpty())
	{
		error = tr("No file selected");
	}
	else
	{
		const QString absolute = resolvedAbsolutePath();
		QFileInfo fileInfo(absolute);
		if (absolute.isEmpty() || !fileInfo.exists())
		{
			error = tr("File not found");
		}
		else
		{
			const QString nativeAbsolute = QDir::toNativeSeparators(fileInfo.absoluteFilePath());

			SF_INFO sfInfo = {};
			SNDFILE* file = sf_wchar_open(nativeAbsolute.toStdWString().c_str(), SFM_READ, &sfInfo);
			if (file == nullptr)
			{
				error = tr("Unsupported file format");
			}
			else
			{
				const int sampleRate = sfInfo.samplerate;
				const double lengthMs = sampleRate > 0 ? sfInfo.frames * 1000.0 / sampleRate : 0.0;
				// A multi-channel readout: the channel count matters here because it
				// must match the number of selected input channels.
				info = tr("%1 ms · %2 samples · %3 Hz · %4 ch")
					.arg(QString::number(lengthMs, 'f', 1))
					.arg(static_cast<qlonglong>(sfInfo.frames))
					.arg(sampleRate)
					.arg(sfInfo.channels);
				sf_close(file);

				const unsigned deviceRate = currentDeviceSampleRate();
				if (deviceRate != 0 && static_cast<unsigned>(sampleRate) != deviceRate)
					warning = tr("Sample rate does not match the device (%1 Hz)").arg(deviceRate);
			}

			ACCESS_MASK mask = GENERIC_READ;
			try
			{
				mask = RegistryHelper::getFileAccessForUser(nativeAbsolute.toStdWString(), SECURITY_LOCAL_SERVICE_RID);
			}
			catch (const RegistryException&)
			{
			}
			const bool readableByService = (mask & GENERIC_READ) == GENERIC_READ || (mask & FILE_GENERIC_READ) == FILE_GENERIC_READ;

			if (!readableByService)
			{
				error = tr("Not readable by the audio service.\nClick the import button to copy it into the config directory.");
				offerImport = true;
			}
			else if (!info.isEmpty())
			{
				const QString configDir = QDir::cleanPath(QFileInfo(filterTable->getConfigPath()).absolutePath());
				const QString fileDir = QDir::cleanPath(fileInfo.absolutePath());
				if (!configDir.isEmpty() && !fileDir.startsWith(configDir, Qt::CaseInsensitive))
					offerImport = true;
			}
		}
	}

	const QString statusText = !error.isEmpty() ? error : warning;
	statusLabel->setVisible(!statusText.isEmpty());
	statusLabel->setText(statusText);
	statusLabel->setProperty("severity", error.isEmpty() ? QStringLiteral("warning") : QStringLiteral("critical"));
	statusLabel->style()->unpolish(statusLabel);
	statusLabel->style()->polish(statusLabel);

	infoLabel->setVisible(!info.isEmpty());
	infoLabel->setText(info);

	importButton->setVisible(offerImport);
}
