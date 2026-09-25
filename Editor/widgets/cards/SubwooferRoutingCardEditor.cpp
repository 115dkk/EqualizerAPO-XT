/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SubwooferRoutingCardEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QStringList>
#include <QToolButton>

#include "SubwooferRouting/Compiler.h"
#include "SubwooferRouting/Crossover.h"
#include "SubwooferRouting/Preset.h"
#include "SubwooferRouting/StateCodec.h"
#include "devices/AbstractAPOInfo.h"
#include "Editor/FilterTable.h"
#include "Editor/helpers/GUIHelper.h"
#include "Editor/SkinManager.h"
#include "Editor/skins/ISkin.h"
#include "Editor/widgets/cards/SubwooferRoutingCardView.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingDefaults.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingEditorDialog.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingStateReads.h"
#include "Editor/widgets/subwooferrouting/SubwooferRoutingUiState.h"
#include "Editor/widgets/cards/FilterCardEditorRegistry.h"

namespace
{
QString fromUtf8(const std::string& text)
{
	return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

std::string toUtf8(const QString& text)
{
	const QByteArray bytes = text.toUtf8();
	return std::string(bytes.constData(),
		static_cast<std::size_t>(bytes.size()));
}

QString profilePayloadPath(QString text)
{
	text = text.trimmed();
	if (text.size() >= 2 && text.front() == QLatin1Char('"')
		&& text.back() == QLatin1Char('"'))
	{
		text = text.mid(1, text.size() - 2);
		text.replace(QStringLiteral("\\\""), QStringLiteral("\""));
		text.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
	}
	return text;
}

QString firstCodecError(
	const subroute::StateDecodeResult& decoded)
{
	if (decoded.errors.empty())
		return QString();

	return fromUtf8(decoded.errors.front().message);
}

QString firstDiagnostic(
	const subroute::ValidationResult& validation,
	subroute::DiagnosticSeverity severity)
{
	for (const subroute::ValidationDiagnostic& diagnostic
		: validation.diagnostics)
	{
		if (diagnostic.severity == severity)
			return fromUtf8(diagnostic.message);
	}
	return QString();
}

QString layoutLabel(const subroute::SubwooferRoutingState& state)
{
	int lfeChannels = 0;
	for (const subroute::PhysicalChannel& channel
		: state.layout.channels)
	{
		if (subwooferroutingeditor::isLfeChannelId(channel.id))
			lfeChannels++;
	}

	const int mainChannels =
		static_cast<int>(state.layout.channels.size()) - lfeChannels;
	return QStringLiteral("%1.%2").arg(mainChannels).arg(lfeChannels);
}

QString presetDisplayName(
	const subroute::PresetDescriptor& preset)
{
	if (subwooferroutingeditor::isIssue246Preset(preset))
		return SubwooferRoutingCardEditor::tr(
			"Issue #246 - Front/Rear 4.1");

	return SubwooferRoutingCardEditor::tr(
		"Built-in preset: %1").arg(fromUtf8(preset.displayName));
}

unsigned tableSampleRate(FilterTable* table)
{
	const std::shared_ptr<AbstractAPOInfo> device =
		table == nullptr ? nullptr : table->getSelectedDevice();
	return device == nullptr ? 0 : device->getSampleRate();
}
}

SubwooferRoutingCardEditor::SubwooferRoutingCardEditor(
	FilterTable* table, const SubwooferRoutingCommand& command,
	const QString& path, unsigned sampleRate, QWidget* parent)
	: SubwooferRoutingCardEditor(table, command, path, sampleRate,
		QString::fromStdWString(command.serialize()), QString(),
		parent)
{
}

SubwooferRoutingCardEditor::SubwooferRoutingCardEditor(
	FilterTable* table, const SubwooferRoutingCommand& command,
	const QString& path, unsigned sampleRate,
	const QString& originalParameters, const QString& parseError,
	QWidget* parent)
	: IFilterGUI(parent), filterTable(table), configPath(path),
	  deviceSampleRate(sampleRate)
{
	setObjectName(QStringLiteral("SubwooferRoutingCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	view = SkinManager::instance()->createSubwooferRoutingCardView(this);
	layout->addWidget(view);
	connect(view, &SubwooferRoutingCardView::openEditorRequested,
		this, &SubwooferRoutingCardEditor::openFullEditor);

	const SkinTokens& tokens = SkinManager::instance()->tokens();
	const QColor actionColor(tokens.text);

	openButton = new QToolButton(view);
	openButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	openButton->setIcon(GUIHelper::tintedIcon(
		QStringLiteral(":/icons/modern/subwoofer-routing.svg"),
		actionColor, 18));
	openButton->setText(tr("Open editor"));
	openButton->setAccessibleName(tr("Open editor"));
	openButton->setToolTip(tr("Open the full subwoofer-routing editor"));
	openButton->setEnabled(true);
	connect(openButton, &QToolButton::clicked,
		this, &SubwooferRoutingCardEditor::openFullEditor);
	view->addActionButton(openButton);

	presetButton = new QToolButton(view);
	presetButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	presetButton->setIcon(GUIHelper::tintedIcon(
		QStringLiteral(":/icons/modern/stage-chain.svg"),
		actionColor, 18));
	presetButton->setText(tr("Preset"));
	presetButton->setAccessibleName(tr("Preset"));
	presetButton->setToolTip(tr("Choose a built-in subwoofer-routing preset"));
	presetButton->setPopupMode(QToolButton::InstantPopup);

	QMenu* presetMenu = new QMenu(presetButton);
	for (const subroute::PresetDescriptor& preset
		: subroute::builtInPresets())
	{
		QAction* action = presetMenu->addAction(
			presetDisplayName(preset));
		const std::string presetId = preset.id;
		connect(action, &QAction::triggered, this,
			[this, presetId]()
			{
				applyPreset(presetId);
			});
	}
	presetButton->setMenu(presetMenu);
	view->addActionButton(presetButton);

	CommandRowInfo rowInfo;
	rowInfo.type = QStringLiteral("subwooferrouting");
	rowInfo.command = QStringLiteral("subwooferrouting");
	SkinManager::instance()->prepareCommandRow(
		rowInfo, nullptr, nullptr, this);

	originalProfileParameters = originalParameters;
	loadCommand(command, parseError);
	refreshCard();
}

void SubwooferRoutingCardEditor::loadCommand(
	const SubwooferRoutingCommand& command, const QString& parseError)
{
	form = command.form;
	loadError = parseError;
	currentState.reset();
	profileMissing = false;

	if (!parseError.isEmpty())
		return;

	if (command.form == SubwooferRoutingCommand::Form::State)
	{
		if (command.payload.empty())
		{
			currentState =
				subwooferroutingeditor::buildDefaultState(
					std::vector<std::wstring>{L"L", L"R"});
			return;
		}

		const std::string payload =
			subwooferRoutingToUtf8(command.payload);
		originalStatePayload = fromUtf8(payload);
		const subroute::StateDecodeResult decoded =
			subroute::decodeState(payload);
		if (!decoded.succeeded())
		{
			loadError = firstCodecError(decoded);
			return;
		}

		currentState = *decoded.state;
		return;
	}

	profilePath = profilePayloadPath(
		QString::fromStdWString(command.payload));
	const QString absolutePath = resolvedProfilePath(profilePath);
	QFile file(absolutePath);
	if (!file.exists())
	{
		profileMissing = true;
		// Name the file only: the resolved path is an implementation detail of
		// the config directory and can leak user directories into screenshots.
		loadError = tr("Linked profile was not found: %1")
			.arg(QFileInfo(absolutePath).fileName());
		return;
	}
	if (!file.open(QIODevice::ReadOnly))
	{
		loadError = tr("Linked profile could not be read: %1")
			.arg(QFileInfo(absolutePath).fileName());
		return;
	}

	const QByteArray bytes = file.readAll();
	const subroute::StateDecodeResult decoded =
		subroute::decodeState(std::string_view(
			bytes.constData(), static_cast<std::size_t>(bytes.size())));
	if (!decoded.succeeded())
	{
		loadError = tr("Linked profile is invalid: %1")
			.arg(firstCodecError(decoded));
		return;
	}

	currentState = *decoded.state;
}

void SubwooferRoutingCardEditor::store(
	QString& command, QString& parameters)
{
	command = QStringLiteral("SubwooferRouting");

	if (form == SubwooferRoutingCommand::Form::Profile)
	{
		parameters = originalProfileParameters;
		if (parameters.isEmpty())
		{
			SubwooferRoutingCommand profile;
			profile.form = SubwooferRoutingCommand::Form::Profile;
			profile.payload = profilePath.toStdWString();
			parameters = QString::fromStdWString(profile.serialize());
		}
		return;
	}

	if (!currentState.has_value())
	{
		parameters = QStringLiteral("State ") + originalStatePayload;
		return;
	}

	const subroute::StateEncodeResult encoded =
		subroute::encodeStateCanonical(*currentState);
	if (!encoded.succeeded())
	{
		parameters = QStringLiteral("State ") + originalStatePayload;
		return;
	}

	parameters = QStringLiteral("State ")
		+ fromUtf8(*encoded.text);
}

void SubwooferRoutingCardEditor::configureChannels(
	std::vector<std::wstring>& channelNames)
{
	Q_UNUSED(channelNames);
}

void SubwooferRoutingCardEditor::openFullEditor()
{
	if (!currentState.has_value())
		return;

	SubwooferRoutingEditorDialog dialog(
		*currentState, deviceSampleRate, this);

	auto commitInlineState = [this, &dialog]()
	{
		currentState = dialog.state();

		// A linked profile is deliberately converted to inline State form.
		// The dialog never writes the linked profile file.
		form = SubwooferRoutingCommand::Form::State;
		originalStatePayload.clear();
		originalProfileParameters.clear();
		profilePath.clear();
		profileMissing = false;
		loadError.clear();

		refreshCard();
		emit updateModel();
	};

	connect(&dialog, &SubwooferRoutingEditorDialog::applied,
		this, commitInlineState);

	if (dialog.exec() == QDialog::Accepted)
		commitInlineState();
}

void SubwooferRoutingCardEditor::applyPreset(
	const std::string& presetId)
{
	const subroute::PresetCreateResult preset =
		subroute::createBuiltInPreset(presetId);
	if (!preset.succeeded())
	{
		loadError = tr("The selected preset could not be created: %1")
			.arg(fromUtf8(preset.error));
		refreshCard();
		return;
	}

	form = SubwooferRoutingCommand::Form::State;
	currentState = *preset.state;
	originalStatePayload.clear();
	originalProfileParameters.clear();
	profilePath.clear();
	profileMissing = false;
	loadError.clear();
	refreshCard();
	emit updateModel();
}

QString SubwooferRoutingCardEditor::resolvedProfilePath(
	const QString& writtenPath) const
{
	QFileInfo profileInfo(writtenPath);
	if (profileInfo.isAbsolute())
		return profileInfo.absoluteFilePath();

	const QFileInfo configInfo(configPath);
	const QString baseDirectory = configInfo.isDir()
		? configInfo.absoluteFilePath()
		: configInfo.absolutePath();
	return QDir(baseDirectory).absoluteFilePath(writtenPath);
}

void SubwooferRoutingCardEditor::refreshCard()
{
	SubwooferRoutingCardState card;
	card.enabled = isEnabled();
	card.linkedProfile =
		form == SubwooferRoutingCommand::Form::Profile;
	card.profileMissing = profileMissing;
	card.errorText = loadError;

	if (card.linkedProfile)
		card.profileName = QFileInfo(profilePath).fileName();

	if (!currentState.has_value())
	{
		card.valid = false;
		card.layoutLabel = tr("Unknown");
		card.headroomTrimDb =
			std::numeric_limits<double>::quiet_NaN();
		view->setState(card);
		return;
	}

	const subroute::SubwooferRoutingState& state = *currentState;
	const subroute::ValidationResult validation =
		subroute::validate(state);
	card.valid = loadError.isEmpty() && !validation.hasErrors();
	if (card.errorText.isEmpty())
	{
		card.errorText = firstDiagnostic(validation,
			subroute::DiagnosticSeverity::Error);
	}
	card.warningText = firstDiagnostic(validation,
		subroute::DiagnosticSeverity::Warning);

	card.layoutLabel = layoutLabel(state);
	card.profileName = card.profileName.isEmpty()
		? fromUtf8(state.metadata.profileName)
		: card.profileName;

	// The card summarizes the first speaker group with a high-pass and the
	// first bass path with a low-pass, each read the way the full editor's
	// row for it reads it (audit #348).
	for (const subroute::SpeakerGroup& group : state.speakerGroups)
	{
		const std::optional<double> highPassHz =
			subwooferroutingeditor::groupHighPass(state, group);
		if (!highPassHz.has_value())
			continue;

		card.highPassHz = *highPassHz;
		const std::optional<subroute::CrossoverRecipe> recipe =
			subwooferroutingeditor::groupRecipe(state, group);
		if (recipe.has_value())
			card.highPassSlope = fromUtf8(
				subroute::crossoverRecipeLabel(*recipe));
		break;
	}

	for (const subroute::Path& path : state.paths)
	{
		if (path.kind != subroute::PathKind::Bass)
			continue;

		const std::optional<double> lowPassHz =
			subwooferroutingeditor::pathLowPass(path);
		if (!lowPassHz.has_value())
			continue;

		card.lowPassHz = *lowPassHz;
		const std::optional<subroute::CrossoverRecipe> recipe =
			subroute::recognizeCrossover(path,
				subroute::BiquadType::LowPass);
		if (recipe.has_value())
			card.lowPassSlope = fromUtf8(
				subroute::crossoverRecipeLabel(*recipe));
		break;
	}

	for (const subroute::Path& path : state.paths)
	{
		if (path.kind != subroute::PathKind::SourceLfe)
			continue;

		bool routed = false;
		for (const subroute::OutputMatrixEntry& output
			: state.outputMatrix)
		{
			for (const subroute::OutputMatrixTerm& term
				: output.terms)
			{
				if (term.sourcePathId == path.id)
				{
					routed = true;
					break;
				}
			}
			if (routed)
				break;
		}

		card.sourceLfePreserved = routed;
		card.sourceLfeGainDb =
			subwooferroutingeditor::sourceLfeEffectiveGainDb(path);
		break;
	}

	card.headroomAuto =
		state.headroom.mode == subroute::HeadroomMode::Auto;
	if (!card.headroomAuto)
	{
		card.headroomTrimDb = state.headroom.manualTrimDb;
	}
	else
	{
		// With no selected device the trim is still worth showing: the
		// analysis barely moves with the sample rate (log sweep up to
		// Nyquist), so a 48 kHz estimate beats an "unavailable" dead end.
		// The UI state owns that rule, so the full editor and its response
		// graph show this same number.
		const std::optional<double> trimDb =
			SubwooferRoutingUiState(state, deviceSampleRate)
				.computedTrimDb();
		card.headroomTrimDb = trimDb.has_value()
			? *trimDb
			: std::numeric_limits<double>::quiet_NaN();
	}

	view->setState(card);
}

REGISTER_FILTER_CARD_EDITOR(SubwooferRouting,
	[](FilterTable* table, const QString& command,
		const QString& parameters) -> IFilterGUI*
	{
		if (command != QStringLiteral("SubwooferRouting"))
			return nullptr;

		SubwooferRoutingCommand parsed;
		QString parseError;
		if (parameters.trimmed().isEmpty())
		{
			parsed.form = SubwooferRoutingCommand::Form::State;
		}
		else
		{
			std::wstring error;
			if (!SubwooferRoutingCommand::parse(
				command.toStdWString(),
				parameters.toStdWString(), parsed, &error))
			{
				parseError = QString::fromStdWString(error);
			}
		}

		const QString configPath =
			table == nullptr ? QString() : table->getConfigPath();
		return new SubwooferRoutingCardEditor(table, parsed,
			configPath, tableSampleRate(table), parameters,
			parseError);
	})
