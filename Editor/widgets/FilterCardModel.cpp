#include "FilterCardModel.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <utility>
#include <variant>

#include "filters/ExpressionCommand.h"
#include "filters/FilterFactoryRegistry.h"
#include "SubwooferRouting/StateCodec.h"
#include "filters/subwooferRouting/SubwooferRoutingCommand.h"
#include "filters/BiQuadCommand.h"
#include "filters/HilbertCommand.h"
#include "filters/VelvetCommand.h"
#include "FilterCommandCatalog.h"

namespace
{
QString middleDotSeparator()
{
	return QStringLiteral(" %1 ").arg(QChar(0x00B7));
}

// The catalog states each command's card identity once; describeLine keeps
// only the per-line summary work below.
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
	// Map the (already upper-cased) config keyword to a BiQuad type, then defer to
	// the engine-side title table (filters/BiQuadCommand.h) so the type -> title
	// strings live in exactly one place. The keyword vocabulary here mirrors
	// the typeExpression regex in describeLine; an unknown keyword falls back to
	// "Biquad", matching biquadTypeTitle(BiQuad::Type)'s own default.
	const QString normalized = code.toUpper();
	BiQuad::Type type;
	if (normalized == QStringLiteral("PK") || normalized == QStringLiteral("PEQ") || normalized == QStringLiteral("MODAL"))
		type = BiQuad::PEAKING;
	else if (normalized == QStringLiteral("LP") || normalized == QStringLiteral("LPQ"))
		type = BiQuad::LOW_PASS;
	else if (normalized == QStringLiteral("HP") || normalized == QStringLiteral("HPQ"))
		type = BiQuad::HIGH_PASS;
	else if (normalized == QStringLiteral("BP"))
		type = BiQuad::BAND_PASS;
	else if (normalized == QStringLiteral("LS") || normalized == QStringLiteral("LSC"))
		type = BiQuad::LOW_SHELF;
	else if (normalized == QStringLiteral("HS") || normalized == QStringLiteral("HSC"))
		type = BiQuad::HIGH_SHELF;
	else if (normalized == QStringLiteral("NO"))
		type = BiQuad::NOTCH;
	else if (normalized == QStringLiteral("AP"))
		type = BiQuad::ALL_PASS;
	else
		return QStringLiteral("Biquad");
	return QString::fromWCharArray(::biquadTypeTitle(type));
}

QString firstCapture(const QRegularExpression& expression, const QString& text)
{
	const QRegularExpressionMatch match = expression.match(text);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

// Reproduces the same labelling the legacy BiQuad GUI shows so the modern card
// agrees with it, including the LSC/HSC/LPQ/HPQ/PEQ/Modal variants.
//
// Deliberately NOT routed through BiQuadCommand::parse: the engine parser is
// intentionally lossy - missing tokens are synthesized as defaults and
// variants are merged (see BiQuadCommand.h) - while this badge must echo only
// what the author wrote, in the author's own spelling. The structural pieces
// already come from shared sources: the command vocabulary from
// FilterFactoryRegistry and the type titles from BiQuadCommand's table (above).
QString summarizeBiquad(const QString& parameters, const QString& code, const QString& state)
{
	static const QRegularExpression frequencyExpression(
		QStringLiteral("\\bFc\\s*([-+0-9.,eE\\x{00A0}]+)\\s*H\\s*z"),
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression gainExpression(
		QStringLiteral("\\bGain\\s*([-+0-9.,eE]+)\\s*dB"),
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression slopeExpression(
		QStringLiteral("^\\s*(?:ON|OFF)\\s+[A-Za-z]+\\s+([-+0-9.,eE]+)\\s*dB"),
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression qExpression(
		QStringLiteral("\\bQ\\s*([-+0-9.,eE]+)"),
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression bandwidthExpression(
		QStringLiteral("\\bBW\\s+Oct\\s*([-+0-9.,eE]+)"),
		QRegularExpression::CaseInsensitiveOption);

	QStringList parts;
	const bool shelf = code == QStringLiteral("LS") || code == QStringLiteral("LSC") || code == QStringLiteral("HS") || code == QStringLiteral("HSC");
	const bool centerFrequency = code == QStringLiteral("LSC") || code == QStringLiteral("HSC");

	const QString freq = firstCapture(frequencyExpression, parameters);
	if (!freq.isEmpty())
		parts.append(QStringLiteral("%1 %2 Hz").arg(shelf ? (centerFrequency ? QStringLiteral("Center") : QStringLiteral("Corner")) : QStringLiteral("Fc"), freq));

	const QString gain = firstCapture(gainExpression, parameters);
	if (!gain.isEmpty())
		parts.append(QStringLiteral("Gain %1 dB").arg(gain));

	if (shelf)
	{
		const QString slope = firstCapture(slopeExpression, parameters);
		if (!slope.isEmpty())
			parts.append(QStringLiteral("Slope %1 dB/Oct").arg(slope));
	}

	const QString q = firstCapture(qExpression, parameters);
	if (!q.isEmpty())
		parts.append(QStringLiteral("Q %1").arg(q));

	const QString bandwidth = firstCapture(bandwidthExpression, parameters);
	if (!bandwidth.isEmpty())
		parts.append(QStringLiteral("BW %1 Oct").arg(bandwidth));

	QString summary = parts.isEmpty() ? parameters.simplified() : parts.join(middleDotSeparator());
	if (state == QStringLiteral("OFF"))
		summary = QStringLiteral("OFF") + middleDotSeparator() + summary;
	return summary;
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

	if (!descriptor.enabled && command != QStringLiteral("#"))
		descriptor.summary = compactWhitespace(parameters);

	// One identity application for every recognized command; the ladder below
	// keeps only the branches that compute a summary from the parameters.
	// Commands whose card is fully described by their catalog row (Preamp,
	// Delay, Device, Stage, Loudness, Eval, the If family) need no branch.
	if (const FilterCommandCatalog::CommandEntry* entry
		= FilterCommandCatalog::entryForKeyword(keyword))
		applyCommandIdentity(descriptor, *entry);

	if (keyword == QStringLiteral("Hilbert"))
	{
		HilbertCommand parsed;
		if (HilbertCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed))
		{
			QStringList parts;
			parts.append(parsed.directionDegrees < 0
				? QStringLiteral("−90°") : QStringLiteral("+90°"));
			parts.append(tr("%1 shifted").arg(parsed.shiftedChannels.size() == 1
				&& parsed.shiftedChannels.front() == L"ALL"
				? QStringLiteral("ALL")
				: QString::number(parsed.shiftedChannels.size())));
			if (!parsed.alignedChannels.empty())
				parts.append(tr("%1 aligned").arg(parsed.alignedChannels.size()));
			descriptor.summary = parts.join(middleDotSeparator());
			for (const std::wstring& channel : parsed.shiftedChannels)
				descriptor.channelBadges.append(QString::fromStdWString(channel));
		}
	}
	else if (keyword == QStringLiteral("Velvet"))
	{
		VelvetCommand parsed;
		if (VelvetCommand::parse(command.toStdWString(),
			parameters.toStdWString(), parsed))
		{
			const int taps = qMax(2, qRound(parsed.parameters.lengthMs
				* parsed.parameters.density / 1000.0));
			descriptor.summary = QStringLiteral("%1%2%3%4%5")
				.arg(parsed.parameters.dynamic ? tr("Dynamic") : tr("Static"),
					middleDotSeparator(),
					tr("%1%").arg(parsed.parameters.amount * 100.0, 0, 'g', 4),
					middleDotSeparator(),
					tr("%1 ms · %2 taps/ch")
						.arg(parsed.parameters.lengthMs, 0, 'g', 5).arg(taps));
		}
	}
	else if (keyword == QStringLiteral("Filter"))
	{
		// EAPO syntax allows numbered filter lines such as `Filter 1:`, `Filter 99:`
		// (commonly emitted by REW, Room EQ Wizard, Dirac, and other tools).
		// canonicalCommand already folds the trailing token away, so both spellings
		// land here and keep the type-specific badge/title/summary.

		// The custom-coefficient IIR grammar ("ON IIR Order N Coefficients ...")
		// keeps the biquad card type so every skin's biquad styling applies;
		// only the badge/title/summary say IIR. Like the biquad summary this
		// echoes what the author wrote with light regexes - the engine
		// (IIRFilterFactory::parseCommand) stays the single grammar owner.
		static const QRegularExpression iirExpression(
			QStringLiteral("^\\s*(ON|OFF)\\s+IIR\\b"), QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression orderExpression(
			QStringLiteral("\\bOrder\\s+([0-9]+)"), QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression coefficientExpression(
			QStringLiteral("\\bCoefficients((?:\\s+[-+0-9.eE]+)+)"), QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch iirMatch = iirExpression.match(parameters);
		if (iirMatch.hasMatch())
		{
			descriptor.badge = QStringLiteral("IIR");
			descriptor.title = tr("IIR filter");

			QStringList parts;
			const QString orderText = firstCapture(orderExpression, parameters);
			if (!orderText.isEmpty())
				parts.append(tr("Order %1").arg(orderText));
			const QString coefficientList = firstCapture(coefficientExpression, parameters).simplified();
			if (!coefficientList.isEmpty())
				parts.append(tr("%1 coefficients").arg(coefficientList.split(QLatin1Char(' ')).size()));
			if (!parts.isEmpty())
			{
				descriptor.summary = parts.join(middleDotSeparator());
				if (iirMatch.captured(1).toUpper() == QStringLiteral("OFF"))
					descriptor.summary = QStringLiteral("OFF") + middleDotSeparator() + descriptor.summary;
			}
		}
		else
		{
			// Recognise the full BiQuadFilterFactory vocabulary (including LSC/HSC
			// shelf-with-slope, LPQ/HPQ Q-form, PEQ alias and Modal) so the card
			// title and summary agree with the legacy GUI.
			static const QRegularExpression typeExpression(
				QStringLiteral("^\\s*(ON|OFF)\\s+(PK|PEQ|MODAL|LPQ|HPQ|LSC|HSC|LP|HP|BP|LS|HS|NO|AP)\\b"),
				QRegularExpression::CaseInsensitiveOption);
			const QRegularExpressionMatch match = typeExpression.match(parameters);
			if (match.hasMatch())
			{
				const QString state = match.captured(1).toUpper();
				const QString code = match.captured(2).toUpper();
				descriptor.badge = code;
				descriptor.title = biquadTypeTitle(code);
				descriptor.summary = summarizeBiquad(parameters, code, state);
			}
		}
	}
	else if (keyword == QStringLiteral("GraphicEQ"))
	{
		int bandCount = parameters.count(';') + 1;
		if (!parameters.trimmed().isEmpty())
			descriptor.summary = tr("%1 bands").arg(bandCount);
	}
	else if (keyword == QStringLiteral("Copy"))
	{
		static const QRegularExpression stepExpression(QStringLiteral("([A-Za-z0-9]+)\\s*="));
		QRegularExpressionMatchIterator matches = stepExpression.globalMatch(parameters);
		QStringList destinations;
		while (matches.hasNext())
			destinations.append(matches.next().captured(1).toUpper());

		int virtualCount = 0;
		for (const QString& destination : destinations)
		{
			if (destination.startsWith('V'))
				virtualCount++;
			else
				descriptor.channelBadges.append(destination);
		}

		if (!destinations.isEmpty())
		{
			if (virtualCount > 0)
				descriptor.summary = tr("%1 steps, %2 virtual").arg(destinations.size()).arg(virtualCount);
			else
				descriptor.summary = tr("%1 steps").arg(destinations.size());
		}
	}
	else if (keyword == QStringLiteral("Channel"))
	{
		descriptor.channelBadges = parseChannelList(parameters);
		descriptor.summary = descriptor.channelBadges.join(' ');
	}
	else if (keyword == QStringLiteral("Include"))
	{
		descriptor.summary = QFileInfo(parameters).fileName();
		if (descriptor.summary.isEmpty())
			descriptor.summary = parameters;
	}
	else if (keyword == QStringLiteral("Convolution"))
	{
		QString fileName = QFileInfo(parameters.section(' ', 0, 0)).fileName();
		if (!fileName.isEmpty())
			descriptor.summary = fileName;
	}
	else if (keyword == QStringLiteral("MultiConvolution"))
	{
		// The grammar differs from Convolution: the first token is the output
		// channel and the remainder is the impulse-response path, so the header
		// reads "<channel> · <file>" (e.g. "L · brir.wav").
		const QString trimmedParams = parameters.trimmed();
		static const QRegularExpression whitespaceExpression(QStringLiteral("\\s"));
		const int split = trimmedParams.indexOf(whitespaceExpression);
		const QString channel = split < 0 ? trimmedParams : trimmedParams.left(split);
		const QString fileName = split < 0 ? QString() : QFileInfo(trimmedParams.mid(split + 1).trimmed()).fileName();
		if (!channel.isEmpty() && !fileName.isEmpty())
			descriptor.summary = channel + middleDotSeparator() + fileName;
		else if (!channel.isEmpty())
			descriptor.summary = channel;
	}
	else if (keyword == QStringLiteral("VSTPlugin"))
	{
		descriptor.summary = QFileInfo(parameters).fileName();
		if (descriptor.summary.isEmpty())
			descriptor.summary = parameters;
	}
	else if (keyword == QStringLiteral("SubwooferRouting"))
	{
		// Keep card-list work bounded. The full editor owns profile I/O and
		// detailed validation; the descriptor only summarizes an in-memory
		// State payload or names a linked profile.
		constexpr qsizetype maximumSummaryPayload = 1024 * 1024;
		if (parameters.size() > maximumSummaryPayload)
		{
			descriptor.summary = tr("invalid state");
		}
		else
		{
			SubwooferRoutingCommand parsed;
			if (!SubwooferRoutingCommand::parse(command.toStdWString(),
				parameters.toStdWString(), parsed))
			{
				descriptor.summary = tr("invalid state");
			}
			else if (parsed.form == SubwooferRoutingCommand::Form::Profile)
			{
				QString path = QString::fromStdWString(parsed.payload);
				if (path.size() >= 2 && path.front() == QLatin1Char('"')
					&& path.back() == QLatin1Char('"'))
				{
					path = path.mid(1, path.size() - 2);
				}
				descriptor.summary = QFileInfo(path).fileName();
				if (descriptor.summary.isEmpty())
					descriptor.summary = path;
			}
			else
			{
				const std::string payload =
					subwooferRoutingToUtf8(parsed.payload);
				const subroute::StateDecodeResult decoded =
					subroute::decodeState(payload);
				if (!decoded.succeeded())
				{
					descriptor.summary = tr("invalid state");
				}
				else
				{
					int lfeChannels = 0;
					for (const subroute::PhysicalChannel& channel
						: decoded.state->layout.channels)
					{
						if (QString::fromUtf8(channel.id.data(),
							static_cast<int>(channel.id.size()))
							.compare(QStringLiteral("LFE"),
								Qt::CaseInsensitive) == 0)
						{
							lfeChannels++;
						}
					}

					// The header speaks the user's language: the layout and
					// the crossover corner. Internal graph statistics (group
					// and path counts) belong to the full editor.
					double crossoverHz = 0.0;
					for (const subroute::Path& path
						: decoded.state->paths)
					{
						for (const subroute::PathStage& stage : path.chain)
						{
							const subroute::BiquadStage* biquad =
								std::get_if<subroute::BiquadStage>(&stage);
							if (biquad == nullptr)
								continue;
							if (biquad->filter.type
								== subroute::BiquadType::HighPass
								|| biquad->filter.type
								== subroute::BiquadType::LowPass)
							{
								crossoverHz = biquad->filter.frequencyHz;
								break;
							}
						}
						if (crossoverHz > 0.0)
							break;
					}

					const int mainChannels =
						static_cast<int>(
							decoded.state->layout.channels.size())
						- lfeChannels;
					const QString layout =
						QStringLiteral("%1.%2")
							.arg(mainChannels)
							.arg(lfeChannels);
					descriptor.summary = crossoverHz > 0.0
						? tr("%1 - crossover %2 Hz")
							.arg(layout)
							.arg(QString::number(crossoverHz, 'g', 5))
						: tr("%1 - full range").arg(layout);
				}
			}
		}
	}
	// The If family and Eval keep their as-written condition text as the
	// summary; each branch keyword is its own catalog row (shared "if" card
	// type, distinct badge), so no branch code is needed here.

	// Rows whose summary is the parameter text as written (raw text lines and
	// the programmatic If/Eval vocabulary) would otherwise fall through to the
	// whole-line fallback and print the command twice ("ENDIF  EndIf:"). An
	// empty summary is the honest reading there: the title already carries
	// everything the line says.
	const bool commandOnlyRow = !command.isEmpty()
		&& (descriptor.type == QStringLiteral("text")
		|| descriptor.type == QStringLiteral("if")
		|| descriptor.type == QStringLiteral("eval"));
	if (descriptor.summary.isEmpty() && !commandOnlyRow)
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
