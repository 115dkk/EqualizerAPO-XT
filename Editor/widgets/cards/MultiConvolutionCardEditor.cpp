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

#include <algorithm>
#include <memory>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

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
#include "Editor/widgets/routing/IRoutingRenderer.h"
#include "Editor/widgets/routing/MultiConvolutionRoutingAdapter.h"
#include "ReferenceCardView.h"
#include "FileReferenceController.h"
#include "helpers/RegistryHelper.h"
#include "helpers/SndfileRAII.h"

MultiConvolutionCardEditor::MultiConvolutionCardEditor(FilterTable* filterTable,
	const std::vector<MultiConvolutionCommand::Mapping>& mappings,
	const QString& path, QWidget* parent)
	: IFilterGUI(parent), filterTable(filterTable),
	  reference(new FileReferenceController(QStringLiteral("multiconvolution"), path, this)),
	  mappings(mappings)
{
	setObjectName(QStringLiteral("MultiConvolutionCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	view = SkinManager::instance()->createReferenceCardView(QStringLiteral("multiconvolution"), this);
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

	// The channel mapping rides under the file reference: a caption row with
	// the virtual-output entry point, then the active skin's routing view.
	QWidget* mappingArea = new QWidget(this);
	mappingArea->setObjectName(QStringLiteral("MultiConvolutionMappingArea"));
	QVBoxLayout* mappingLayout = new QVBoxLayout(mappingArea);
	mappingLayout->setContentsMargins(4, 0, 4, 2);
	mappingLayout->setSpacing(4);

	QHBoxLayout* captionRow = new QHBoxLayout();
	captionRow->setContentsMargins(0, 0, 0, 0);
	mappingCaption = new QLabel(tr("Channel mapping"), mappingArea);
	mappingCaption->setObjectName(QStringLiteral("MultiConvolutionMappingCaption"));
	captionRow->addWidget(mappingCaption);
	captionRow->addStretch(1);

	addChannelButton = new QToolButton(mappingArea);
	addChannelButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	addChannelButton->setText(QStringLiteral("+"));
	addChannelButton->setToolTip(tr("Add an output channel (a new name creates a virtual channel)"));
	connect(addChannelButton, SIGNAL(clicked()), this, SLOT(addOutputChannel()));
	captionRow->addWidget(addChannelButton);
	mappingLayout->addLayout(captionRow);

	routingLayout = new QVBoxLayout();
	routingLayout->setContentsMargins(0, 0, 0, 0);
	mappingLayout->addLayout(routingLayout);
	layout->addWidget(mappingArea);

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
	cmd.mappings = mappings;
	cmd.path = reference->writtenPath().toStdWString();
	parameters = QString::fromStdWString(cmd.serialize());
}

void MultiConvolutionCardEditor::configureChannels(std::vector<std::wstring>& channelNames)
{
	rowChannels = channelNames;
	rebuildRoutingView();
}

void MultiConvolutionCardEditor::routingEdited()
{
	if (routingView == nullptr)
		return;

	std::vector<MultiConvolutionCommand::Mapping> edited = MultiConvolutionRoutingAdapter::toMappings(routingView->assignments());
	if (edited.empty())
	{
		// Removing the last connection would leave a line the grammar cannot
		// round-trip (a bare path). Keep the previous mapping and rebuild the
		// view from it instead of persisting the empty state.
		rebuildRoutingView();
		return;
	}

	mappings = std::move(edited);
	emit updateModel();
}

void MultiConvolutionCardEditor::addOutputChannel()
{
	bool accepted = false;
	const QString name = QInputDialog::getText(this, tr("Add output channel"),
			tr("Channel name (an unknown name creates a virtual channel):"),
			QLineEdit::Normal, QString(), &accepted).trimmed();
	if (!accepted || name.isEmpty() || name.contains(QLatin1Char(' ')))
		return;

	const std::wstring channel = name.toStdWString();
	auto alreadyThere = [&channel](const std::vector<std::wstring>& list) {
		return std::find(list.begin(), list.end(), channel) != list.end();
	};
	if (!alreadyThere(rowChannels) && !alreadyThere(extraTargets))
		extraTargets.push_back(channel);
	rebuildRoutingView();
}

void MultiConvolutionCardEditor::rebuildRoutingView()
{
	// The old widgets may be the signal sender that led here (routingEdited),
	// so they cannot be deleted synchronously; detaching and hiding them keeps
	// them out of layout and paint until deleteLater lands.
	if (routingView != nullptr)
	{
		routingLayout->removeWidget(routingView);
		routingView->hide();
		routingView->deleteLater();
		routingView = nullptr;
	}
	if (routingHint != nullptr)
	{
		routingLayout->removeWidget(routingHint);
		routingHint->hide();
		routingHint->deleteLater();
		routingHint = nullptr;
	}

	IRoutingRenderer* renderer = SkinManager::instance()->routingRenderer();

	// Without a readable file the mapping cannot be edited: the view could not
	// know what the simple form ("every channel") expands to, and an edit
	// would persist a wrong expansion. Show why instead.
	if (renderer == nullptr || fileChannelCount <= 0)
	{
		routingHint = new QLabel(tr("Select a readable impulse response file to edit the channel mapping."), this);
		routingHint->setObjectName(QStringLiteral("MultiConvolutionMappingHint"));
		routingHint->setWordWrap(true);
		routingLayout->addWidget(routingHint);
		addChannelButton->setEnabled(false);
		return;
	}
	addChannelButton->setEnabled(true);

	std::vector<Assignment> assignments = MultiConvolutionRoutingAdapter::toAssignments(mappings, fileChannelCount);

	// The output side: channels in scope at this row plus the virtual outputs
	// added in this session (the seeded rows with empty sums never reach the
	// config line).
	std::vector<std::wstring> targets = rowChannels;
	for (const std::wstring& extra : extraTargets)
		if (std::find(targets.begin(), targets.end(), extra) == targets.end())
			targets.push_back(extra);

	RoutingPortModel portModel;
	portModel.fixedSources = MultiConvolutionRoutingAdapter::sourcePorts(fileChannelCount, mappings);

	routingView = renderer->create(assignments, targets, portModel, this);
	routingLayout->addWidget(routingView);
	connect(routingView, SIGNAL(routingChanged()), this, SLOT(routingEdited()));
}

void MultiConvolutionCardEditor::chooseFile()
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

void MultiConvolutionCardEditor::pathCommitted(const QString& text)
{
	reference->setWrittenPath(text);
	updateFileInfo();
	emit updateModel();
}

void MultiConvolutionCardEditor::importToConfig()
{
	if (filterTable == nullptr)
		return;

	reference->setResolvedPath(resolvedAbsolutePath());
	if (!reference->importIntoConfig(this, filterTable->getConfigPath()))
		return;
	updateFileInfo();
	emit updateModel();
}

QString MultiConvolutionCardEditor::resolvedAbsolutePath() const
{
	if (filterTable == nullptr)
		return QString();

	reference->resolveAgainstConfig(filterTable->getConfigPath());
	return reference->resolvedPath();
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
	const QString absolute = resolvedAbsolutePath();
	ReferenceCardState state = reference->describe(tr("No file selected"));
	fileChannelCount = 0;
	bool offerImport = false;
	if (!state.missing)
	{
		QFileInfo fileInfo(absolute);
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
				// The channel count doubles as the routing view's source-port
				// list: every file channel becomes a mappable input.
				state.readout << tr("%1 ms").arg(QString::number(lengthMs, 'f', 1))
					<< tr("%1 samples").arg(static_cast<qlonglong>(sfInfo.frames))
					<< tr("%1 Hz").arg(sampleRate)
					<< tr("%1 ch").arg(sfInfo.channels);
				fileChannelCount = sfInfo.channels;
				const unsigned deviceRate = currentDeviceSampleRate();
				if (deviceRate != 0 && static_cast<unsigned>(sampleRate) != deviceRate)
				{
					state.statusText = tr("Sample rate does not match the device (%1 Hz)").arg(deviceRate);
					state.statusSeverity = ReferenceCardState::Severity::Warning;
				}
				else
				{
					// A mapping that references a channel the file does not have
					// contributes silence; surface it before the user wonders why
					// an ear stays quiet.
					unsigned highest = 0;
					bool any = false;
					for (const MultiConvolutionCommand::Mapping& mapping : mappings)
						for (const MultiConvolutionCommand::IrChannelRef& ref : mapping.irChannels)
						{
							highest = std::max(highest, ref.channel);
							any = true;
						}
					if (any && (int)highest >= fileChannelCount)
					{
						state.statusText = tr("Mapping references channel %1, but the file has %2 channels").arg(highest).arg(fileChannelCount);
						state.statusSeverity = ReferenceCardState::Severity::Warning;
					}
				}
			}

			// See ConvolutionCardEditor: the audio service only holds rights
			// inside the config directory. The offscreen gallery skips the probe.
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
	rebuildRoutingView();
}

#include "FilterCardEditorRegistry.h"

REGISTER_FILTER_CARD_EDITOR(MultiConvolution, [](FilterTable* filterTable, const QString& command, const QString& parameters) -> IFilterGUI* {
	// MultiConvolutionCommand owns the line grammar (mapping and simple forms);
	// the card hosts the mapping in the skin's routing view. Text the parser
	// rejects falls through rather than opening a card with no path, which the
	// first interaction would write back over what the author had.
	//
	// The empty line is the exception: the Insert menu drops a bare
	// "MultiConvolution:" template and the parser rejects it (it wants both a
	// target and a path), but there is nothing to lose yet and an empty card is
	// exactly what the user asked for. The legacy GUI factory claims that line
	// for the same reason.
	MultiConvolutionCommand cmd;
	if (!MultiConvolutionCommand::parse(command.trimmed().toStdWString(), parameters.toStdWString(), cmd)
		&& !parameters.trimmed().isEmpty())
		return nullptr;
	return new MultiConvolutionCardEditor(filterTable, cmd.mappings, QString::fromStdWString(cmd.path));
})
