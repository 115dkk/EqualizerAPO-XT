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
#include "Editor/widgets/subwooferrouting/SubwooferRoutingEditorDialog.h"
#include "Editor/widgets/cards/FilterCardEditorRegistry.h"

namespace
{
constexpr double kDefaultCrossoverHz = 80.0;
constexpr double kButterworthQ = 0.7071067811865476;

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

subroute::PathStage polarityStage()
{
	return subroute::PolarityStage{false};
}

subroute::PathStage delayStage()
{
	return subroute::DelayStage{0.0};
}

subroute::PathStage equalizerSlotsStage()
{
	return subroute::EqualizerSlotsStage{};
}

subroute::Path makePath(const std::string& id,
	subroute::PathKind kind,
	const std::vector<subroute::SourceMixTerm>& sourceMix,
	std::optional<subroute::BiquadType> crossoverType)
{
	subroute::Path path;
	path.id = id;
	path.kind = kind;
	path.sourceMix = sourceMix;
	path.chain.push_back(polarityStage());
	if (crossoverType.has_value())
	{
		subroute::BiquadFilter filter;
		filter.type = *crossoverType;
		filter.frequencyHz = kDefaultCrossoverHz;
		filter.q = kButterworthQ;
		filter.gainDb = 0.0;
		path.chain.push_back(subroute::BiquadStage{filter});
	}
	path.chain.push_back(delayStage());
	path.chain.push_back(equalizerSlotsStage());
	return path;
}

std::vector<std::string> usableChannelIds(
	const std::vector<std::wstring>& channels)
{
	std::vector<std::string> result;
	for (const std::wstring& channel : channels)
	{
		const std::string id = subwooferRoutingToUtf8(channel);
		if (!id.empty() && subroute::isValidStableId(id)
			&& std::find(result.begin(), result.end(), id) == result.end())
		{
			result.push_back(id);
		}
	}
	return result;
}

bool isLfeId(const std::string& id)
{
	QString value = fromUtf8(id);
	return value.compare(QStringLiteral("LFE"),
		Qt::CaseInsensitive) == 0;
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
		if (isLfeId(channel.id))
			lfeChannels++;
	}

	const int mainChannels =
		static_cast<int>(state.layout.channels.size()) - lfeChannels;
	return QStringLiteral("%1.%2").arg(mainChannels).arg(lfeChannels);
}

const subroute::Path* representativeCrossoverPath(
	const subroute::SubwooferRoutingState& state,
	subroute::BiquadType type)
{
	for (const subroute::Path& path : state.paths)
	{
		for (const subroute::PathStage& stage : path.chain)
		{
			const subroute::BiquadStage* biquad =
				std::get_if<subroute::BiquadStage>(&stage);
			if (biquad != nullptr && biquad->filter.type == type)
				return &path;
		}
	}
	return nullptr;
}

const subroute::BiquadFilter* firstSection(
	const subroute::Path& path, subroute::BiquadType type)
{
	for (const subroute::PathStage& stage : path.chain)
	{
		const subroute::BiquadStage* biquad =
			std::get_if<subroute::BiquadStage>(&stage);
		if (biquad != nullptr && biquad->filter.type == type)
			return &biquad->filter;
	}
	return nullptr;
}

double sourceLfeGainDb(const subroute::Path& path)
{
	double gainDb = path.preGainDb + path.postGainDb;
	if (!path.sourceMix.empty())
	{
		const double gain = std::abs(
			path.sourceMix.front().gainLinear);
		if (gain > 0.0)
			gainDb += 20.0 * std::log10(gain);
	}

	for (const subroute::PathStage& stage : path.chain)
	{
		const subroute::GainStage* gain =
			std::get_if<subroute::GainStage>(&stage);
		if (gain != nullptr)
			gainDb += gain->gainDb;
	}
	return gainDb;
}

subroute::PrepareSpec prepareSpecFor(
	const subroute::SubwooferRoutingState& state,
	unsigned sampleRate)
{
	subroute::PrepareSpec spec;
	spec.sampleRate = sampleRate;
	spec.maximumBlockSize = 1024;
	spec.channelLayout.reserve(state.layout.channels.size());
	for (const subroute::PhysicalChannel& channel : state.layout.channels)
		spec.channelLayout.push_back(channel.id);
	return spec;
}

double compiledTrimDb(const subroute::HeadroomAnalysis& analysis)
{
	return analysis.appliedTrimDb;
}
QString presetDisplayName(
	const subroute::PresetDescriptor& preset)
{
	if (preset.id == subroute::kIssue246FrontRear41PresetId)
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

namespace subwooferroutingeditor
{
subroute::SubwooferRoutingState buildDefaultState(
	const std::vector<std::wstring>& deviceChannels)
{
	std::vector<std::string> channels =
		usableChannelIds(deviceChannels);
	if (channels.size() < 2)
		channels = {"L", "R"};

	auto lfe = std::find_if(channels.begin(), channels.end(),
		[](const std::string& id)
		{
			return isLfeId(id);
		});

	std::vector<std::string> mainChannels;
	for (const std::string& id : channels)
	{
		if (!isLfeId(id))
			mainChannels.push_back(id);
	}
	if (mainChannels.size() < 2)
	{
		channels = {"L", "R"};
		mainChannels = channels;
		lfe = channels.end();
	}

	std::string left = mainChannels[0];
	std::string right = mainChannels[1];
	for (const std::string& id : mainChannels)
	{
		const QString channel = fromUtf8(id);
		if (channel.compare(QStringLiteral("L"),
			Qt::CaseInsensitive) == 0)
		{
			left = id;
		}
		else if (channel.compare(QStringLiteral("R"),
			Qt::CaseInsensitive) == 0)
		{
			right = id;
		}
	}

	const bool hasLfe = lfe != channels.end();
	const std::string lfeId = hasLfe ? *lfe : std::string();

	subroute::SubwooferRoutingState state;
	for (const std::string& id : channels)
		state.layout.channels.push_back({id, id});

	state.metadata.creatingApp = "Equalizer APO XT";
	state.metadata.creatingAppVersion = "";
	state.metadata.profileName = "";
	state.headroom.mode = subroute::HeadroomMode::Auto;
	state.headroom.manualTrimDb = 0.0;

	const std::optional<subroute::BiquadType> mainCrossover =
		hasLfe
		? std::optional<subroute::BiquadType>(
			subroute::BiquadType::HighPass)
		: std::nullopt;

	state.paths.push_back(makePath("FrontLeft",
		subroute::PathKind::Main, {{left, 1.0}},
		mainCrossover));
	state.paths.push_back(makePath("FrontRight",
		subroute::PathKind::Main, {{right, 1.0}},
		mainCrossover));

	subroute::SpeakerGroup group;
	group.id = "Front";
	group.displayName = "Front";
	group.mainPathIds = {"FrontLeft", "FrontRight"};

	if (hasLfe)
	{
		state.paths.push_back(makePath("FrontBass",
			subroute::PathKind::Bass,
			{{left, 1.0}, {right, 1.0}},
			subroute::BiquadType::LowPass));
		group.bassPathId = "FrontBass";

		state.paths.push_back(makePath("SourceLFE",
			subroute::PathKind::SourceLfe,
			{{lfeId, 1.0}}, std::nullopt));
	}

	state.speakerGroups.push_back(group);

	state.outputMatrix.push_back(
		{left, subroute::OutputMode::Replace,
			{{"FrontLeft", 0.0}}});
	state.outputMatrix.push_back(
		{right, subroute::OutputMode::Replace,
			{{"FrontRight", 0.0}}});

	if (hasLfe)
	{
		state.outputMatrix.push_back(
			{lfeId, subroute::OutputMode::Replace,
				{{"FrontBass", 0.0}, {"SourceLFE", 0.0}}});
	}

	return state;
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

	const subroute::Path* highPassPath =
		representativeCrossoverPath(state,
			subroute::BiquadType::HighPass);
	if (highPassPath != nullptr)
	{
		card.highPassHz = firstSection(*highPassPath,
			subroute::BiquadType::HighPass)->frequencyHz;
		const std::optional<subroute::CrossoverRecipe> recipe =
			subroute::recognizeCrossover(*highPassPath,
				subroute::BiquadType::HighPass);
		if (recipe.has_value())
			card.highPassSlope = fromUtf8(
				subroute::crossoverRecipeLabel(*recipe));
	}

	const subroute::Path* lowPassPath =
		representativeCrossoverPath(state,
			subroute::BiquadType::LowPass);
	if (lowPassPath != nullptr)
	{
		card.lowPassHz = firstSection(*lowPassPath,
			subroute::BiquadType::LowPass)->frequencyHz;
		const std::optional<subroute::CrossoverRecipe> recipe =
			subroute::recognizeCrossover(*lowPassPath,
				subroute::BiquadType::LowPass);
		if (recipe.has_value())
			card.lowPassSlope = fromUtf8(
				subroute::crossoverRecipeLabel(*recipe));
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
		card.sourceLfeGainDb = sourceLfeGainDb(path);
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
		const unsigned analysisRate =
			deviceSampleRate > 0 ? deviceSampleRate : 48000;
		const subroute::CompileResult compiled =
			subroute::compile(state,
				prepareSpecFor(state, analysisRate));
		if (compiled.headroom.has_value())
		{
			card.headroomTrimDb =
				compiledTrimDb(*compiled.headroom);
		}
		else
		{
			card.headroomTrimDb =
				std::numeric_limits<double>::quiet_NaN();
		}
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
