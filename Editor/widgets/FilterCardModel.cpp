/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FilterCardModel.h"

#include <QRegularExpression>

#include <utility>

#include "filters/ExpressionCommand.h"
#include "filters/FilterFactoryRegistry.h"
#include "filters/BiQuadCommand.h"
#include "filters/ChannelCommand.h"
#include "filters/HilbertCommand.h"
#include "filters/NewChannelCommand.h"
#include "FilterCommandCatalog.h"
#include "Editor/widgets/routing/ChannelIdentity.h"

namespace
{
// The catalog states each command's card identity once; describeLine keeps
// only the per-line badge/title/chip work below.
void applyCommandIdentity(FilterCardDescriptor& descriptor,
	const FilterCommandCatalog::CommandEntry& entry)
{
	descriptor.type = QLatin1String(entry.type);
	descriptor.badge = QLatin1String(entry.badge);
	descriptor.title = FilterCommandCatalog::title(entry);
	descriptor.color = QLatin1String(entry.color);
	descriptor.routeType = entry.routeType;
}

// The names a NewChannel line declares, through the engine's own parser
// (upper-cased, duplicates dropped).
QStringList newChannelNames(const QString& parameters)
{
	NewChannelCommand cmd;
	NewChannelCommand::parse(L"NewChannel", parameters.toStdWString(), cmd);
	QStringList names;
	for (const std::wstring& name : cmd.names)
		names.append(QString::fromStdWString(name));
	return names;
}

QString biquadTypeTitle(const QString& code)
{
	// Map the (already upper-cased) config keyword to a card title. The
	// English titles mirror the engine's table (filters/BiQuadCommand.h,
	// which the log lines use) but go through tr(): the card header is
	// user-facing, and the untranslated "Peaking" between Korean titles was
	// a field complaint (type-scale round). describeLine only passes a
	// keyword biquadTypeFromName accepted; an unknown one falls back to
	// "Biquad", matching the engine's own default.
	const QString normalized = code.toUpper();
	if (normalized == QStringLiteral("PK") || normalized == QStringLiteral("PEQ") || normalized == QStringLiteral("MODAL"))
		return FilterCardModel::tr("Peaking");
	if (normalized == QStringLiteral("LP") || normalized == QStringLiteral("LPQ"))
		return FilterCardModel::tr("Low-pass");
	if (normalized == QStringLiteral("HP") || normalized == QStringLiteral("HPQ"))
		return FilterCardModel::tr("High-pass");
	if (normalized == QStringLiteral("BP"))
		return FilterCardModel::tr("Band-pass");
	if (normalized == QStringLiteral("LS") || normalized == QStringLiteral("LSC"))
		return FilterCardModel::tr("Low-shelf");
	if (normalized == QStringLiteral("HS") || normalized == QStringLiteral("HSC"))
		return FilterCardModel::tr("High-shelf");
	if (normalized == QStringLiteral("NO"))
		return FilterCardModel::tr("Notch");
	if (normalized == QStringLiteral("AP"))
		return FilterCardModel::tr("All-pass");
	return FilterCardModel::tr("Biquad");
}

QStringList channelNames(const std::vector<std::wstring>& values)
{
	QStringList names;
	for (const std::wstring& value : values)
		names.append(QString::fromStdWString(value));
	return names;
}

FilterCardRowScope scopeFromFlow(const ChannelFlowAtLine& flow)
{
	FilterCardRowScope scope;
	scope.indent = flow.indent;
	scope.logic = flow.logicDepth;
	if (!flow.selectionIsAll)
		scope.channels = channelNames(flow.selected);
	return scope;
}
}

QString FilterCardModel::compactWhitespace(const QString& text)
{
	return text.simplified();
}

QString FilterCardModel::canonicalCommand(const QString& key)
{
	// The one QString <-> std::wstring crossing for command classification, and
	// nothing more. The rule lives in the engine
	// (FilterFactoryRegistry::canonicalCommand) so that the Editor cannot hold a
	// second opinion about what a line is - which is exactly how "preamp:" came
	// to open a live card for a line the engine never ran.
	return QString::fromStdWString(
		FilterFactoryRegistry::canonicalCommand(key.toStdWString()));
}

QString FilterCardModel::assembleLine(const QString& command, const QString& parameters,
	const QString& replacedLine)
{
	if (command == QStringLiteral("#"))
		return parameters.isEmpty() ? QStringLiteral("#") : QStringLiteral("# ") + parameters;
	const QString line = command + QStringLiteral(": ") + parameters;
	// Same spelling as the enable toggle writes, so edit and toggle round-trip.
	return isDisabledCommandLine(replacedLine) ? QStringLiteral("# ") + line : line;
}

bool FilterCardModel::isDisabledCommandLine(const QString& line)
{
	QString trimmed = line.trimmed();
	if (!trimmed.startsWith('#'))
		return false;

	trimmed = trimmed.mid(1).trimmed();
	int colon = trimmed.indexOf(':');
	if (colon < 0)
		return false;

	// Only a real command can be a *disabled* command. "# Comment: ..." and
	// "# todo: ..." are notes, and so is "# preamp: ..." - the engine would not
	// run that line even with the '#' removed, so offering to re-enable it
	// would promise something the engine never delivers.
	return !canonicalCommand(trimmed.left(colon)).isEmpty();
}

bool FilterCardModel::isPureCommentLine(const QString& line)
{
	QString trimmed = line.trimmed();
	return trimmed.startsWith('#') && !isDisabledCommandLine(line);
}

QStringList FilterCardModel::headerChannels(const FilterCardDescriptor& descriptor,
	const std::vector<std::wstring>& deviceChannels)
{
	QStringList channels;
	if (descriptor.type == QStringLiteral("newchannel"))
		return channels;
	if (descriptor.channelBadgesAreCopyTargets)
	{
		for (const QString& target : descriptor.channelBadges)
		{
			if (!ChannelIdentity::isVirtual(target, deviceChannels))
				channels.append(target);
		}
	}
	else
	{
		channels = descriptor.channelBadges;
	}
	if (channels.isEmpty() && FilterCommandCatalog::channelSelectionGatesType(descriptor.type))
		channels = descriptor.scopeChannels;
	return channels;
}

bool FilterCardModel::hostsSharedRawBody(const QString& type, bool dynamicLine)
{
	return type == QStringLiteral("text") || type == QStringLiteral("if")
		|| type == QStringLiteral("eval") || dynamicLine;
}

bool FilterCardModel::opensRoutingView(const FilterCardDescriptor& descriptor)
{
	return descriptor.type == QStringLiteral("copy") && !descriptor.dynamicLine;
}

QString FilterCardModel::commandForLine(const QString& line, QString* parameters)
{
	QString trimmed = line.trimmed();
	if (trimmed.startsWith('#'))
	{
		trimmed = trimmed.mid(1).trimmed();
		if (!trimmed.contains(':'))
		{
			if (parameters != nullptr)
				*parameters = trimmed;
			return QString();
		}
	}

	int colon = trimmed.indexOf(':');
	if (colon < 0)
	{
		if (parameters != nullptr)
			*parameters = trimmed;
		return QString();
	}

	if (parameters != nullptr)
		*parameters = trimmed.mid(colon + 1).trimmed();
	return trimmed.left(colon).trimmed();
}

FilterCardDescriptor FilterCardModel::describeLine(const QString& line, int depth)
{
	FilterCardDescriptor descriptor;
	descriptor.depth = depth;
	descriptor.enabled = !line.trimmed().startsWith('#');

	// Blank / whitespace-only lines (including the trailing newline that almost
	// every config file ends with) are a dedicated spacer type so the row
	// widget can render a thin separator instead of a full-height empty card.
	if (line.trimmed().isEmpty())
	{
		descriptor.type = QStringLiteral("spacer");
		descriptor.canToggleEnabled = false;
		return descriptor;
	}

	if (isPureCommentLine(line))
	{
		QString trimmed = line.trimmed();
		descriptor.command = QStringLiteral("#");
		if (const FilterCommandCatalog::CommandEntry* entry
			= FilterCommandCatalog::entryForKeyword(QStringLiteral("#")))
			applyCommandIdentity(descriptor, *entry);
		descriptor.summary = compactWhitespace(trimmed.mid(1));
		descriptor.canToggleEnabled = false;
		return descriptor;
	}

	QString parameters;
	QString command = commandForLine(line, &parameters);
	// The engine decides what this line is; the card only decides how to draw
	// it. A key nothing claims (plain prose, an unknown keyword, a lowercase
	// "preamp:") keeps the raw-text card, which is what the engine does with it.
	const QString keyword = canonicalCommand(command);
	descriptor.command = command;
	descriptor.parameters = parameters;
	descriptor.dynamicLine = InlineExpression::containsExpression(parameters.toStdWString());
	descriptor.title = command.isEmpty() ? tr("Text") : command;
	descriptor.summary = compactWhitespace(parameters);
	descriptor.type = QStringLiteral("text");
	descriptor.badge = QStringLiteral("TXT");
	descriptor.color = QStringLiteral("#64748b");

	// One identity application for every recognized command; the ladder below
	// keeps only the branches that derive non-summary card facts (badges,
	// titles, channel chips) from the parameters.
	const FilterCommandCatalog::CommandEntry* entry
		= FilterCommandCatalog::entryForKeyword(keyword);
	if (entry != nullptr)
		applyCommandIdentity(descriptor, *entry);

	if (keyword == QStringLiteral("Hilbert"))
	{
		HilbertCommand parsed;
		if (HilbertCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed))
		{
			for (const std::wstring& channel : parsed.shiftedChannels)
				descriptor.channelBadges.append(QString::fromStdWString(channel));
		}
	}
	else if (keyword == QStringLiteral("Filter"))
	{
		// EAPO syntax allows numbered filter lines such as `Filter 1:`, `Filter 99:`
		// (commonly emitted by REW, Room EQ Wizard, Dirac, and other tools).
		// canonicalCommand already folds the trailing token away, so both spellings
		// land here and keep the type-specific badge and title.

		// The custom-coefficient IIR grammar ("ON IIR Order N Coefficients ...")
		// keeps the biquad card type so every skin's biquad styling applies;
		// only the badge and title say IIR. The engine
		// (IIRCommand::parse) stays the single grammar owner.
		static const QRegularExpression iirExpression(QStringLiteral("^\\s*(ON|OFF)\\s+IIR\\b"));
		const QRegularExpressionMatch iirMatch = iirExpression.match(parameters);
		if (iirMatch.hasMatch())
		{
			descriptor.badge = QStringLiteral("IIR");
			descriptor.title = tr("IIR filter");
		}
		else
		{
			// The type word is judged by the engine's own vocabulary, case
			// included (biquadTypeFromName), so a line the engine rejects as
			// "on pk" is not drawn as a peaking filter (audit #348 TD-44). The
			// engine reads ON and OFF in capitals too.
			static const QRegularExpression typeExpression(QStringLiteral("^\\s*(ON|OFF)\\s+([A-Za-z]+)"));
			const QRegularExpressionMatch match = typeExpression.match(parameters);
			BiQuad::Type type;
			if (match.hasMatch() && biquadTypeFromName(match.captured(2).toStdWString(), type))
			{
				const QString code = match.captured(2).toUpper();
				descriptor.badge = code;
				descriptor.title = biquadTypeTitle(code);
			}
		}
	}
	else if (keyword == QStringLiteral("Copy"))
	{
		static const QRegularExpression stepExpression(QStringLiteral("([A-Za-z0-9]+)\\s*="));
		QRegularExpressionMatchIterator matches = stepExpression.globalMatch(parameters);
		QStringList destinations;
		while (matches.hasNext())
			destinations.append(matches.next().captured(1).toUpper());

		// Every destination: which are virtual depends on the device, which
		// the line does not know (headerChannels decides).
		descriptor.channelBadges = destinations;
		descriptor.channelBadgesAreCopyTargets = true;
	}
	else if (keyword == QStringLiteral("Channel"))
	{
		ChannelCommand cmd;
		ChannelCommand::parse(L"Channel", parameters.toStdWString(), cmd);
		descriptor.channelBadges = channelNames(cmd.channels);
	}
	else if (keyword == QStringLiteral("NewChannel"))
	{
		// headerChannels() badges none of the declared names: they are virtual,
		// and a name that is not is the
		// reason the engine skips the line, which the body's chips show.
		descriptor.channelBadges = newChannelNames(parameters);
	}

	// Header summaries do not echo or paraphrase a recognized command's
	// parameters (round 2 of the raw-exposure cleanup: a VSTPlugin header
	// used to print its whole ChunkData blob, and even the friendly biquad
	// readout restated what the body already shows as controls). The rows
	// that keep text are the ones whose text IS the content: prose keeps the
	// whole line, an unknown "key: value" keeps its value text from the
	// default above, comment rows returned early with their note, and the
	// raw-body hosts (If family, Eval, dynamic lines) keep their condition
	// or expression, because their body is the line editor itself, not a
	// second presentation of the parameters.
	if (entry != nullptr
		&& !hostsSharedRawBody(descriptor.type, descriptor.dynamicLine))
		descriptor.summary.clear();
	else if (descriptor.summary.isEmpty() && command.isEmpty())
		descriptor.summary = compactWhitespace(line);

	return descriptor;
}

QString FilterCardModel::badgeIconResource(const QString& type, const QString& badge)
{
	// The type-to-pictogram mapping is the catalog's (the entry's icon).
	return FilterCommandCatalog::badgeIconResource(type, badge);
}

QString FilterCardModel::commandIconResource(const QString& command, const QString& parameters)
{
	const QString normalized = command.trimmed();
	if (normalized.compare(QStringLiteral("filter"), Qt::CaseInsensitive) == 0)
	{
		// The Filter command splits by its response-type token; an unmatched
		// line keeps the generic peaking curve.
		const QString upper = QStringLiteral(" ") + parameters.toUpper() + QStringLiteral(" ");
		for (const FilterCommandCatalog::BiquadCurveEntry& curve
			: FilterCommandCatalog::biquadCurves())
			if (upper.contains(QStringLiteral(" %1 ").arg(QLatin1String(curve.code))))
				return FilterCommandCatalog::iconResource(curve.icon);
		return FilterCommandCatalog::iconResource("eq-peaking");
	}

	const FilterCommandCatalog::CommandEntry* entry
		= FilterCommandCatalog::entryForCommandWord(normalized);
	// An unmapped word returns empty so raw text falls back to its monogram.
	return entry == nullptr ? QString() : FilterCommandCatalog::iconResource(entry->icon);
}

void FilterCardModel::applyScope(FilterCardDescriptor& descriptor, const FilterCardRowScope& scope)
{
	descriptor.depth = scope.indent;
	descriptor.logicDepth = scope.logic;
	descriptor.scopeChannels = scope.channels;
}

FilterCardDescriptor FilterCardModel::describeRow(const QString& line, const FilterCardRowScope& scope)
{
	FilterCardDescriptor descriptor = describeLine(line);
	applyScope(descriptor, scope);
	return descriptor;
}

FilterCardRowScope FilterCardModel::scopeOf(const FilterCardDescriptor& descriptor)
{
	FilterCardRowScope scope;
	scope.indent = descriptor.depth;
	scope.logic = descriptor.logicDepth;
	scope.channels = descriptor.scopeChannels;
	return scope;
}

FilterCardDocumentPlan FilterCardModel::prepareDocument(const QList<QString>& lines,
	const ChannelFlowContext& context)
{
	FilterCardDocumentPlan document;
	document.rows.reserve(lines.size());

	std::vector<ChannelFlowLine> flowLines;
	flowLines.reserve(static_cast<size_t>(lines.size()));
	for (const QString& line : lines)
	{
		ChannelFlowLine flowLine;
		QString parameters;
		flowLine.command = commandForLine(line, &parameters).toStdWString();
		flowLine.parameters = parameters.toStdWString();
		flowLine.enabled = !line.trimmed().startsWith('#');
		flowLines.push_back(std::move(flowLine));
	}
	document.channelFlow = computeChannelFlow(flowLines, context);

	for (qsizetype i = 0; i < lines.size(); i++)
	{
		FilterCardBuildPlan row;
		row.descriptor = describeLine(lines[i]);
		row.scope = scopeFromFlow(document.channelFlow[static_cast<size_t>(i)]);
		applyScope(row.descriptor, row.scope);
		document.rows.append(std::move(row));
	}
	return document;
}
