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
#include "filters/HilbertCommand.h"
#include "FilterCommandCatalog.h"

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

QString biquadTypeTitle(const QString& code)
{
	// Map the (already upper-cased) config keyword to a card title. The
	// English titles mirror the engine's table (filters/BiQuadCommand.h,
	// which the log lines use) but go through tr(): the card header is
	// user-facing, and the untranslated "Peaking" between Korean titles was
	// a field complaint (type-scale round). The keyword vocabulary here
	// mirrors the typeExpression regex in describeLine; an unknown keyword
	// falls back to "Biquad", matching the engine's own default.
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

// keyword is the engine's canonical command (FilterCardModel::canonicalCommand),
// so a lowercase "if:" or "channel:" moves neither axis here for the same reason
// it opens no scope in the engine.
FilterCardRowScope advanceScope(bool enabled, const QString& keyword,
	const QStringList& channelBadges, QStringList& activeChannels, int& channelDepth, int& ifDepth)
{
	FilterCardRowScope scope;
	if (enabled && keyword == QStringLiteral("Channel"))
	{
		scope.indent = ifDepth;
		scope.logic = ifDepth;
		const bool selectsAll = channelBadges.isEmpty() || channelBadges.contains(QStringLiteral("ALL"));
		channelDepth = selectsAll ? 0 : 1;
		activeChannels = selectsAll ? QStringList() : channelBadges;
	}
	else if (enabled && keyword == QStringLiteral("If"))
	{
		scope.indent = channelDepth + ifDepth;
		scope.logic = ifDepth;
		ifDepth++;
	}
	else if (enabled && (keyword == QStringLiteral("ElseIf") || keyword == QStringLiteral("Else")))
	{
		scope.indent = channelDepth + qMax(0, ifDepth - 1);
		scope.logic = ifDepth;
	}
	else if (enabled && keyword == QStringLiteral("EndIf"))
	{
		scope.indent = channelDepth + qMax(0, ifDepth - 1);
		scope.logic = ifDepth;
		ifDepth = qMax(0, ifDepth - 1);
	}
	else
	{
		scope.indent = channelDepth + ifDepth;
		scope.logic = ifDepth;
	}
	scope.channels = activeChannels;
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

bool FilterCardModel::hasInlineExpressions(const QString& parameters)
{
	// Cheap pre-check before the lexer: a line without a backtick can have
	// no expression segment.
	if (!parameters.contains(QLatin1Char('`')))
		return false;
	const std::vector<InlineExpression::Segment> segments = InlineExpression::split(parameters.toStdWString());
	for (const InlineExpression::Segment& segment : segments)
		if (segment.isExpression)
			return true;
	return false;
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

QStringList FilterCardModel::parseChannelList(const QString& text)
{
	static const QRegularExpression whitespaceExpression(QStringLiteral("\\s+"));
	QString normalized = text;
	normalized.replace(',', ' ');
	QStringList result = normalized.split(whitespaceExpression, Qt::SkipEmptyParts);
	for (QString& channel : result)
		channel = channel.trimmed().toUpper();
	return result;
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
	descriptor.dynamicLine = hasInlineExpressions(parameters);
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
		// (IIRFilterFactory::parseCommand) stays the single grammar owner.
		static const QRegularExpression iirExpression(
			QStringLiteral("^\\s*(ON|OFF)\\s+IIR\\b"), QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch iirMatch = iirExpression.match(parameters);
		if (iirMatch.hasMatch())
		{
			descriptor.badge = QStringLiteral("IIR");
			descriptor.title = tr("IIR filter");
		}
		else
		{
			// Recognise the full BiQuadFilterFactory vocabulary (including LSC/HSC
			// shelf-with-slope, LPQ/HPQ Q-form, PEQ alias and Modal) so the card
			// title agrees with the legacy GUI.
			static const QRegularExpression typeExpression(
				QStringLiteral("^\\s*(ON|OFF)\\s+(PK|PEQ|MODAL|LPQ|HPQ|LSC|HSC|LP|HP|BP|LS|HS|NO|AP)\\b"),
				QRegularExpression::CaseInsensitiveOption);
			const QRegularExpressionMatch match = typeExpression.match(parameters);
			if (match.hasMatch())
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

		for (const QString& destination : destinations)
		{
			if (!destination.startsWith('V'))
				descriptor.channelBadges.append(destination);
		}
	}
	else if (keyword == QStringLiteral("Channel"))
	{
		descriptor.channelBadges = parseChannelList(parameters);
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
	if (type == QStringLiteral("biquad"))
	{
		// Prefix matching folds the factory's long vocabulary onto the eight
		// response-curve glyphs (LPQ rides with LP, LSC with LS, PEQ/MODAL
		// with PK); an unparsed biquad ("BQUAD") shows the generic peaking
		// curve rather than a letter chunk, mirroring the picker's fallback.
		for (const FilterCommandCatalog::BiquadCurveEntry& curve
			: FilterCommandCatalog::biquadCurves())
			if (badge.startsWith(QLatin1String(curve.code)))
				return FilterCommandCatalog::iconResource(curve.icon);
		return FilterCommandCatalog::iconResource("eq-peaking");
	}
	if (type == QStringLiteral("convolution"))
	{
		// The badge splits the siblings: one shared type, two pictograms.
		return commandIconResource(badge == QStringLiteral("MCONV")
			? QStringLiteral("multiconvolution") : QStringLiteral("convolution"));
	}
	if (type == QStringLiteral("hilbert"))
		return commandIconResource(QStringLiteral("hilbert"));
	if (type == QStringLiteral("velvet"))
		return commandIconResource(QStringLiteral("velvet"));

	if (type == QStringLiteral("comment"))
		return commandIconResource(QStringLiteral("#"));
	if (type == QStringLiteral("vst"))
		return commandIconResource(QStringLiteral("vstplugin"));
	if (type == QStringLiteral("loudness"))
		return commandIconResource(QStringLiteral("loudnesscorrection"));
	return commandIconResource(type);
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

QVector<FilterCardRowScope> FilterCardModel::calculateScopes(const QList<QString>& lines)
{
	QVector<FilterCardRowScope> scopes;
	scopes.reserve(lines.size());

	// The two depth axes are independent: Channel opens a flat 0/1 grouping for
	// everything after it, If opens a nestable scope that EndIf closes. Only
	// enabled lines move either axis - a commented-out If/EndIf is a comment to
	// the engine too. An unbalanced EndIf clamps at zero instead of going
	// negative, mirroring how the engine just ignores the stray line.
	QStringList activeChannels;
	int channelDepth = 0;
	int ifDepth = 0;
	for (const QString& line : lines)
	{
		const bool enabled = !line.trimmed().startsWith('#');
		QString parameters;
		const QString keyword = canonicalCommand(commandForLine(line, &parameters));
		const QStringList channels = keyword == QStringLiteral("Channel")
			? parseChannelList(parameters) : QStringList();
		scopes.append(advanceScope(enabled, keyword, channels, activeChannels, channelDepth, ifDepth));
	}

	return scopes;
}

QVector<FilterCardBuildPlan> FilterCardModel::prepareRows(const QList<QString>& lines)
{
	QVector<FilterCardBuildPlan> plans;
	plans.reserve(lines.size());

	QStringList activeChannels;
	int channelDepth = 0;
	int ifDepth = 0;
	for (const QString& line : lines)
	{
		FilterCardBuildPlan plan;
		plan.descriptor = describeLine(line);
		plan.scope = advanceScope(plan.descriptor.enabled,
			canonicalCommand(plan.descriptor.command), plan.descriptor.channelBadges,
			activeChannels, channelDepth, ifDepth);
		plan.descriptor.depth = plan.scope.indent;
		plan.descriptor.logicDepth = plan.scope.logic;
		plan.descriptor.scopeChannels = plan.scope.channels;
		plans.append(std::move(plan));
	}
	return plans;
}

QVector<int> FilterCardModel::calculateDepths(const QList<QString>& lines)
{
	const QVector<FilterCardRowScope> scopes = calculateScopes(lines);
	QVector<int> depths;
	depths.reserve(scopes.size());
	for (const FilterCardRowScope& scope : scopes)
		depths.append(scope.indent);
	return depths;
}
