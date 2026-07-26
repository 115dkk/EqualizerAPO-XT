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

#include "devices/AbstractAPOInfo.h"
#include "Editor/FilterTable.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/helpers/ConvolutionPathHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/import/ConfigDependencyScanner.h"
#include "Editor/import/ImportDialog.h"
#include "Editor/import/ImportExecutor.h"
#include "ReferenceCardView.h"
#include "FileReferenceController.h"
#include "filters/ConvolutionCommand.h"
#include "helpers/RegistryHelper.h"
#include "helpers/SndfileRAII.h"

ConvolutionCardEditor::ConvolutionCardEditor(FilterTable* filterTable, const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable),
	  reference(new FileReferenceController(QStringLiteral("convolution"), path, this))
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
	cmd.path = reference->writtenPath().toStdWString();
	parameters = QString::fromStdWString(cmd.serialize());
}

void ConvolutionCardEditor::chooseFile()
{
	if (filterTable == nullptr)
		return;

	const QString configPath = filterTable->getConfigPath();
	const QString currentAbsolute = resolvedAbsolutePath();
	QFileInfo startInfo(currentAbsolute.isEmpty() ? configPath : currentAbsolute);

	const QString selected = reference->chooseExistingFile(
		this, tr("Select impulse response file"), startInfo.absolutePath(),
		tr("Impulse response (*.wav *.flac *.ogg)"),
		QFileInfo(configPath).absolutePath(),
		reference->writtenPath().isEmpty() ? QString() : startInfo.fileName());
	if (!selected.isEmpty())
	{
		updateFileInfo();
		emit updateModel();
	}
}

void ConvolutionCardEditor::pathCommitted(const QString& text)
{
	reference->setWrittenPath(text);
	updateFileInfo();
	emit updateModel();
}

void ConvolutionCardEditor::importToConfig()
{
	if (filterTable == nullptr)
		return;

	reference->setResolvedPath(resolvedAbsolutePath());
	if (!reference->importIntoConfig(this, filterTable->getConfigPath()))
		return;
	updateFileInfo();
	emit updateModel();
}

QString ConvolutionCardEditor::resolvedAbsolutePath() const
{
	if (filterTable == nullptr)
		return QString();

	reference->resolveAgainstConfig(filterTable->getConfigPath());
	return reference->resolvedPath();
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
	const QString absolute = resolvedAbsolutePath();
	ReferenceCardState state = reference->describe(tr("No file selected"));
	bool offerImport = false;
	if (!state.missing)
	{
		QFileInfo fileInfo(absolute);
			// Impulse-response readout. The Editor runs as the interactive user,
			// so it usually reads the file even when the audio service cannot;
			// the readout stays visible next to any permission status below.
			SF_INFO sfInfo = {};
			sndfile::Handle file(sf_wchar_open(state.fullPath.toStdWString().c_str(), SFM_READ, &sfInfo));
			if (!file)
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
				if (!FileReferenceController::isReadableByAudioService(state.fullPath))
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

	const bool locate = state.missing && !reference->writtenPath().isEmpty();
	chooseButton->setText(locate ? tr("Locate...") : QString());
	chooseButton->setToolTip(locate ? tr("Locate the missing file") : tr("Select impulse response file"));

	view->setState(state);
	importButton->setVisible(offerImport);
}

#include "FilterCardEditorRegistry.h"
#include "filters/ConvolutionCommand.h"

REGISTER_FILTER_CARD_EDITOR(Convolution, [](FilterTable* filterTable, const QString& command, const QString& parameters) -> IFilterGUI* {
	// ConvolutionCommand owns the line grammar; the path it yields preserves
	// the author's quotes/variables so store() round-trips the config text. A
	// key the parser rejects (only "Convolution" exactly runs, unlike the
	// numbered Filter form) must fall through instead of opening a card with an
	// empty path, which the first interaction would write back over the line.
	ConvolutionCommand cmd;
	if (!ConvolutionCommand::parse(command.trimmed().toStdWString(), parameters.toStdWString(), cmd))
		return nullptr;
	return new ConvolutionCardEditor(filterTable, QString::fromStdWString(cmd.path));
})
