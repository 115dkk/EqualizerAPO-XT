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
#include <QCoreApplication>
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
#include "Editor/widgets/cards/FileReferenceController.h"
#include "Editor/widgets/cards/ReferenceCardView.h"
#include "Editor/widgets/cards/SubwooferRoutingCardState.h"
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

unsigned tableSampleRate(FilterTable* table)
{
	return table == nullptr ? 0 : table->deviceSampleRate();
}

// One decimal, rounded first so a value that rounds to zero prints "0.0"
// and never "-0.0".
QString oneDecimal(double value)
{
	const double rounded = std::round(value * 10.0) / 10.0;
	return QString::number(std::abs(rounded) < 0.05 ? 0.0 : rounded, 'f', 1);
}

// A gain with its sign: "+10.0", "-3.5", "+0.0".
QString signedOneDecimal(double value)
{
	const QString text = oneDecimal(value);
	return text.startsWith(QLatin1Char('-')) ? text : QStringLiteral("+") + text;
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

	// The profile is edited in a window of its own, the way a plugin's panel
	// is, so the card is a reference card: the profile's name, a few facts
	// and two worded actions, drawn by each skin's reference view.
	view = SkinManager::instance()->createReferenceCardView(
		QStringLiteral("subwoofer"), this);
	layout->addWidget(view);
	connect(view, &ReferenceCardView::nameActivated,
		this, &SubwooferRoutingCardEditor::openFullEditor);

	// Browse comes first, as on the sibling reference cards: the folder
	// button that picks the linked profile file, which the views turn into
	// their LOCATE form while the file is missing. It exists only for a line
	// that links a file; showCard hides it for an embedded profile.
	profileReference = new FileReferenceController(
		QStringLiteral("subwoofer"), QString(), this);
	browseButton = new QToolButton(view);
	browseButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	browseButton->setIcon(GUIHelper::tintedIcon(
		QStringLiteral(":/icons/modern/folder-open.svg"),
		QColor(SkinManager::instance()->tokens().text), 18));
	connect(browseButton, &QToolButton::clicked,
		this, &SubwooferRoutingCardEditor::chooseProfileFile);
	view->addActionButton(ReferenceCardView::ActionRole::Browse, browseButton);

	// The other two actions are words, with no icon, in every skin. The
	// editor button's enabled state and tooltip follow the loaded state
	// (showCard).
	openButton = new QToolButton(view);
	openButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	openButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	openButton->setText(tr("Open editor"));
	openButton->setAccessibleName(tr("Open editor"));
	connect(openButton, &QToolButton::clicked,
		this, &SubwooferRoutingCardEditor::openFullEditor);
	view->addActionButton(ReferenceCardView::ActionRole::OpenPanel, openButton);

	// The preset menu stays available while the profile is missing or
	// invalid: choosing a preset is the repair the card offers.
	presetButton = new QToolButton(view);
	presetButton->setObjectName(QStringLiteral("FilterCardIconButton"));
	presetButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	presetButton->setText(tr("Preset"));
	presetButton->setAccessibleName(tr("Preset"));
	presetButton->setToolTip(tr("Choose a built-in subwoofer-routing preset"));
	presetButton->setPopupMode(QToolButton::InstantPopup);

	QMenu* presetMenu = new QMenu(presetButton);
	for (const subroute::PresetDescriptor& preset
		: subroute::builtInPresets())
	{
		QAction* action = presetMenu->addAction(
			fromUtf8(subwooferroutingeditor::presetDisplayName(preset)));
		const std::string presetId = preset.id;
		connect(action, &QAction::triggered, this,
			[this, presetId]()
			{
				applyPreset(presetId);
			});
	}
	presetButton->setMenu(presetMenu);
	view->addActionButton(ReferenceCardView::ActionRole::Preset, presetButton);

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

// Picks a profile file and links the line to it: the file dialog the sibling
// reference cards use, opened at the current file (or the configuration's
// folder), with the chosen path written relative to the configuration's
// folder when it lies under it. The file is then read the way loadCommand
// reads any linked profile, so a missing, unreadable or invalid file is
// reported as it is when the line is loaded. Cancel changes nothing.
void SubwooferRoutingCardEditor::chooseProfileFile()
{
	const QFileInfo configInfo(configPath);
	const QString configDirectory = configPath.isEmpty()
		? QString()
		: (configInfo.isDir() ? configInfo.absoluteFilePath()
			: configInfo.absolutePath());

	QString startDirectory = configDirectory;
	QString selectedName;
	if (!profilePath.isEmpty())
	{
		const QFileInfo current(resolvedProfilePath(profilePath));
		startDirectory = current.absolutePath();
		selectedName = current.fileName();
	}

	profileReference->setWrittenPath(profilePath);
	const QString selected = profileReference->chooseExistingFile(this,
		tr("Subwoofer routing profile"), startDirectory,
		tr("Subwoofer routing profiles (*.swxt.json)"), configDirectory,
		selectedName);
	if (selected.isEmpty())
		return;

	SubwooferRoutingCommand linked;
	linked.form = SubwooferRoutingCommand::Form::Profile;
	linked.payload = profileReference->writtenPath().toStdWString();
	originalStatePayload.clear();
	originalProfileParameters.clear();
	loadCommand(linked, QString());
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
		showCard(card);
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

	showCard(card);
}

// The reference card's state for the summary refreshCard computed. Facts are
// printed only while a profile state is loaded: a missing or unreadable
// profile has none to print, and its error sentence says what is wrong.
void SubwooferRoutingCardEditor::showCard(
	const SubwooferRoutingCardState& card)
{
	const bool loaded = currentState.has_value();

	ReferenceCardState state;
	state.kind = QStringLiteral("subwoofer");
	if (card.linkedProfile)
	{
		// Named and located the way the other reference cards name their
		// file: the file name as written, the as-written folder (or, for an
		// absolute reference, the resolved one) as the location.
		const QString normalized = QDir::fromNativeSeparators(profilePath);
		const QFileInfo asWritten(normalized);
		const QString resolved = resolvedProfilePath(profilePath);
		state.name = asWritten.fileName();
		state.absolutePath = QDir::isAbsolutePath(normalized);
		state.missing = profileMissing;
		// The views read an empty as-written reference as one that was never
		// set (an empty slot); a linked profile that is not found is broken,
		// so the reference as written is handed over too. Nothing edits it:
		// the card offers no path editor.
		state.editText = profilePath;
		if (!profileMissing)
			state.fullPath = QDir::toNativeSeparators(resolved);
		if (state.absolutePath)
			state.directory = QDir::toNativeSeparators(
				QFileInfo(resolved).absolutePath());
		else if (asWritten.path() != QStringLiteral("."))
			state.directory = QDir::toNativeSeparators(asWritten.path());
	}
	else
	{
		state.name = card.profileName.isEmpty()
			? tr("Embedded profile")
			: card.profileName;
	}

	if (loaded)
	{
		state.readout.append(card.layoutLabel);
		if (card.highPassHz > 0.0 || card.lowPassHz > 0.0)
			state.readout.append(
				SubwooferRoutingCardText::crossoverSummary(card));
		state.readout.append(card.sourceLfePreserved
			? tr("LFE %1 dB").arg(signedOneDecimal(card.sourceLfeGainDb))
			: tr("LFE off"));
		if (std::isfinite(card.headroomTrimDb))
			state.readout.append(
				tr("Trim %1 dB").arg(oneDecimal(card.headroomTrimDb)));
		else if (card.headroomAuto)
			state.readout.append(tr("Trim auto"));
	}

	// One status sentence at most, error first (the status contract in
	// SubwooferRoutingCardState.h). A linked profile that is not found gets
	// none: the views' broken state already says so beside the file's name
	// (NOT FOUND, MISSING, the red lamp), as it does for a missing include or
	// impulse file, and the sentence only repeated it - and pushed the name
	// out of the one-line skins.
	if (!state.missing && !card.errorText.isEmpty())
	{
		state.statusText = card.errorText;
		state.statusSeverity = ReferenceCardState::Severity::Critical;
	}
	else if (!state.missing && !card.warningText.isEmpty())
	{
		state.statusText = card.warningText;
		state.statusSeverity = ReferenceCardState::Severity::Warning;
	}

	state.nameClickable = loaded;

	// The folder button belongs to a line that links a profile file, healthy
	// or missing; an embedded profile has no file to pick. While the file is
	// missing it is the Locate entry (the Include card's words).
	const bool locate = state.missing && !profilePath.isEmpty();
	browseButton->setVisible(card.linkedProfile);
	// Each text is its own statement: lupdate files a tr() that follows
	// QCoreApplication::translate(...) in the same expression under the
	// QCoreApplication context, where the card would never find it.
	const QString locateText = QCoreApplication::translate("IncludeCardEditor", "Locate...");
	const QString locateToolTip = QCoreApplication::translate("IncludeCardEditor", "Locate the missing file");
	const QString chooseToolTip = tr("Choose profile file");
	browseButton->setText(locate ? locateText : QString());
	browseButton->setToolTip(locate ? locateToolTip : chooseToolTip);

	// Without a loaded state the editor has nothing to open; the button says
	// why instead of doing nothing on a click.
	openButton->setEnabled(loaded);
	openButton->setToolTip(loaded
		? tr("Open the full subwoofer-routing editor")
		: tr("The profile could not be loaded. Choose a preset or correct the line."));

	view->setState(state);
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
