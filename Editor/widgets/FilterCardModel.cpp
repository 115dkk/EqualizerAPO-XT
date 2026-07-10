#include "FilterCardModel.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include "filters/ExpressionCommand.h"
#include "filters/FilterFactoryRegistry.h"
#include "filters/BiQuadCommand.h"

namespace
{
bool isKnownConfigCommand(const QString& command)
{
	QString normalized = command.trimmed().toLower();
	if (normalized.startsWith(QStringLiteral("filter")))
		return true;

	// Derive the recognized command vocabulary from the engine's single source
	// of truth (FilterFactoryRegistry) instead of duplicating it here, so this
	// stays in sync as factories are added or renamed. Registry keys are
	// case-sensitive while this comparison is lowercased, so each keyword is
	// lowered on the way in. "Comment" is not a factory command, so it is added
	// explicitly. "Filter" is already handled by the startsWith check above, so
	// its presence in the registry set is harmless.
	static const QSet<QString> knownCommands = []() {
		QSet<QString> commands;
		for (const std::wstring& keyword : FilterFactoryRegistry::knownConfigCommands())
			commands.insert(QString::fromStdWString(keyword).toLower());
		commands.insert(QStringLiteral("comment"));
		return commands;
	}();
	return knownCommands.contains(normalized);
}

QString biquadTypeTitle(const QString& code)
{
	// Map the (already upper-cased) config keyword to a BiQuad type, then defer to
	// the engine-side title table (filters/BiQuadCommand.h) so the type -> title
	// strings live in exactly one place (F042). The keyword vocabulary here mirrors
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
// agrees with it for the LSC/HSC/LPQ/HPQ/PEQ/Modal variants that the previous
// regex did not recognize.
//
// Deliberately NOT routed through BiQuadCommand::parse (audit #146 TD019,
// skipped): the engine parser is intentionally lossy - missing tokens are
// synthesized as defaults and variants are merged (see BiQuadCommand.h, audit
// #109 F007) - while this badge must echo only what the author wrote, in the
// author's own spelling. The structural pieces already come from shared
// sources: the command vocabulary from FilterFactoryRegistry and the type
// titles from BiQuadCommand's table (above).
QString summarizeBiquad(const QString& parameters, const QString& code, const QString& state)
{
	QStringList parts;
	const bool shelf = code == QStringLiteral("LS") || code == QStringLiteral("LSC") || code == QStringLiteral("HS") || code == QStringLiteral("HSC");
	const bool centerFrequency = code == QStringLiteral("LSC") || code == QStringLiteral("HSC");

	const QString freq = firstCapture(QRegularExpression(QStringLiteral("\\bFc\\s*([-+0-9.,eE\\x{00A0}]+)\\s*H\\s*z"), QRegularExpression::CaseInsensitiveOption), parameters);
	if (!freq.isEmpty())
		parts.append(QStringLiteral("%1 %2 Hz").arg(shelf ? (centerFrequency ? QStringLiteral("Center") : QStringLiteral("Corner")) : QStringLiteral("Fc"), freq));

	const QString gain = firstCapture(QRegularExpression(QStringLiteral("\\bGain\\s*([-+0-9.,eE]+)\\s*dB"), QRegularExpression::CaseInsensitiveOption), parameters);
	if (!gain.isEmpty())
		parts.append(QStringLiteral("Gain %1 dB").arg(gain));

	if (shelf)
	{
		const QRegularExpression slopeExpression(QStringLiteral("^\\s*(?:ON|OFF)\\s+[A-Za-z]+\\s+([-+0-9.,eE]+)\\s*dB"), QRegularExpression::CaseInsensitiveOption);
		const QString slope = firstCapture(slopeExpression, parameters);
		if (!slope.isEmpty())
			parts.append(QStringLiteral("Slope %1 dB/Oct").arg(slope));
	}

	const QString q = firstCapture(QRegularExpression(QStringLiteral("\\bQ\\s*([-+0-9.,eE]+)"), QRegularExpression::CaseInsensitiveOption), parameters);
	if (!q.isEmpty())
		parts.append(QStringLiteral("Q %1").arg(q));

	const QString bandwidth = firstCapture(QRegularExpression(QStringLiteral("\\bBW\\s+Oct\\s*([-+0-9.,eE]+)"), QRegularExpression::CaseInsensitiveOption), parameters);
	if (!bandwidth.isEmpty())
		parts.append(QStringLiteral("BW %1 Oct").arg(bandwidth));

	QString summary = parts.isEmpty() ? parameters.simplified() : parts.join(QStringLiteral(" \xC2\xB7 "));
	if (state == QStringLiteral("OFF"))
		summary = QStringLiteral("OFF \xC2\xB7 ") + summary;
	return summary;
}
}

QString FilterCardModel::compactWhitespace(const QString& text)
{
	return text.simplified();
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

	return isKnownConfigCommand(trimmed.left(colon));
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
	QString normalized = text;
	normalized.replace(',', ' ');
	QStringList result = normalized.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
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
	// every config file ends with) used to fall through to the generic "TXT
	// Text" branch and produce a full-height card with an empty title. Mark
	// them as a dedicated spacer so the row widget can render a thin separator
	// instead of an empty card.
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
		descriptor.title = tr("Comment");
		descriptor.summary = compactWhitespace(trimmed.mid(1));
		descriptor.type = QStringLiteral("comment");
		descriptor.badge = QStringLiteral("#");
		descriptor.color = QStringLiteral("#94a3b8");
		descriptor.canToggleEnabled = false;
		return descriptor;
	}

	QString parameters;
	QString command = commandForLine(line, &parameters);
	QString commandLower = command.toLower();
	descriptor.command = command;
	descriptor.title = command.isEmpty() ? tr("Text") : command;
	descriptor.summary = compactWhitespace(parameters);
	descriptor.type = QStringLiteral("text");
	descriptor.badge = QStringLiteral("TXT");
	descriptor.color = QStringLiteral("#64748b");

	if (!descriptor.enabled && command != QStringLiteral("#"))
		descriptor.summary = compactWhitespace(parameters);

	if (commandLower == QStringLiteral("preamp"))
	{
		descriptor.type = QStringLiteral("preamp");
		descriptor.badge = QStringLiteral("PRE");
		descriptor.title = tr("Preamp");
		descriptor.color = QStringLiteral("#f59e0b");
	}
	else if (commandLower == QStringLiteral("delay"))
	{
		descriptor.type = QStringLiteral("delay");
		descriptor.badge = QStringLiteral("DLY");
		descriptor.title = tr("Delay");
		descriptor.color = QStringLiteral("#14b8a6");
	}
	else if (commandLower == QStringLiteral("filter") || commandLower.startsWith(QStringLiteral("filter ")))
	{
		// EAPO syntax allows numbered filter lines such as `Filter 1:`, `Filter 99:`
		// (commonly emitted by REW, Room EQ Wizard, Dirac, and other tools). The
		// exact-match check that used to live here treated `Filter 1` as a generic
		// text command, which stripped the type-specific badge/title/summary off
		// every BiQuad card produced by those tools.
		descriptor.type = QStringLiteral("biquad");
		descriptor.badge = QStringLiteral("BQUAD");
		descriptor.title = tr("Biquad");
		descriptor.color = QStringLiteral("#22c55e");

		// The custom-coefficient IIR grammar ("ON IIR Order N Coefficients ...")
		// keeps the biquad card type so every skin's biquad styling applies;
		// only the badge/title/summary say IIR. Like the biquad summary this
		// echoes what the author wrote with light regexes - the engine
		// (IIRFilterFactory::parseCommand) stays the single grammar owner.
		QRegularExpression iirExpression(QStringLiteral("^\\s*(ON|OFF)\\s+IIR\\b"), QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatch iirMatch = iirExpression.match(parameters);
		if (iirMatch.hasMatch())
		{
			descriptor.badge = QStringLiteral("IIR");
			descriptor.title = tr("IIR filter");

			QStringList parts;
			const QString orderText = firstCapture(QRegularExpression(QStringLiteral("\\bOrder\\s+([0-9]+)"), QRegularExpression::CaseInsensitiveOption), parameters);
			if (!orderText.isEmpty())
				parts.append(tr("Order %1").arg(orderText));
			const QString coefficientList = firstCapture(QRegularExpression(QStringLiteral("\\bCoefficients((?:\\s+[-+0-9.eE]+)+)"), QRegularExpression::CaseInsensitiveOption), parameters).simplified();
			if (!coefficientList.isEmpty())
				parts.append(tr("%1 coefficients").arg(coefficientList.split(QLatin1Char(' ')).size()));
			if (!parts.isEmpty())
			{
				descriptor.summary = parts.join(QStringLiteral(" %1 ").arg(QChar(0x00B7)));
				if (iirMatch.captured(1).toUpper() == QStringLiteral("OFF"))
					descriptor.summary = QStringLiteral("OFF %1 ").arg(QChar(0x00B7)) + descriptor.summary;
			}
		}
		else
		{
			// Recognise the full BiQuadFilterFactory vocabulary (including LSC/HSC
			// shelf-with-slope, LPQ/HPQ Q-form, PEQ alias and Modal) so the card
			// title and summary agree with the legacy GUI. The previous regex only
			// matched PK/LP/HP/LS/HS/NO/AP/BP and silently fell back to "Biquad".
			QRegularExpression typeExpression(QStringLiteral("^\\s*(ON|OFF)\\s+(PK|PEQ|MODAL|LPQ|HPQ|LSC|HSC|LP|HP|BP|LS|HS|NO|AP)\\b"), QRegularExpression::CaseInsensitiveOption);
			QRegularExpressionMatch match = typeExpression.match(parameters);
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
	else if (commandLower == QStringLiteral("graphiceq"))
	{
		descriptor.type = QStringLiteral("graphiceq");
		descriptor.badge = QStringLiteral("GEQ");
		descriptor.title = tr("Graphic EQ");
		descriptor.color = QStringLiteral("#8b5cf6");

		int bandCount = parameters.count(';') + 1;
		if (!parameters.trimmed().isEmpty())
			descriptor.summary = tr("%1 bands").arg(bandCount);
	}
	else if (commandLower == QStringLiteral("copy"))
	{
		descriptor.type = QStringLiteral("copy");
		descriptor.badge = QStringLiteral("CPY");
		descriptor.title = tr("Copy");
		descriptor.color = QStringLiteral("#06b6d4");
		descriptor.routeType = true;

		QRegularExpression stepExpression(QStringLiteral("([A-Za-z0-9]+)\\s*="));
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
	else if (commandLower == QStringLiteral("channel"))
	{
		descriptor.type = QStringLiteral("channel");
		descriptor.badge = QStringLiteral("CH");
		descriptor.title = tr("Channel");
		descriptor.color = QStringLiteral("#3b82f6");
		descriptor.routeType = true;
		descriptor.channelBadges = parseChannelList(parameters);
		descriptor.summary = descriptor.channelBadges.join(' ');
	}
	else if (commandLower == QStringLiteral("include"))
	{
		descriptor.type = QStringLiteral("include");
		descriptor.badge = QStringLiteral("INC");
		descriptor.title = tr("Include");
		descriptor.color = QStringLiteral("#64748b");
		descriptor.routeType = true;
		descriptor.summary = QFileInfo(parameters).fileName();
		if (descriptor.summary.isEmpty())
			descriptor.summary = parameters;
	}
	else if (commandLower == QStringLiteral("convolution"))
	{
		descriptor.type = QStringLiteral("convolution");
		descriptor.badge = QStringLiteral("CONV");
		descriptor.title = tr("Convolution");
		descriptor.color = QStringLiteral("#ec4899");
		QString fileName = QFileInfo(parameters.section(' ', 0, 0)).fileName();
		if (!fileName.isEmpty())
			descriptor.summary = fileName;
	}
	else if (commandLower == QStringLiteral("multiconvolution"))
	{
		// Shares the convolution row type so skins style it like the single-input
		// convolution card. The grammar differs: the first token is the output
		// channel and the remainder is the impulse-response path, so the header
		// reads "<channel> · <file>" (e.g. "L · brir.wav").
		descriptor.type = QStringLiteral("convolution");
		descriptor.badge = QStringLiteral("MCONV");
		descriptor.title = tr("MultiConvolution");
		descriptor.color = QStringLiteral("#ec4899");
		const QString trimmedParams = parameters.trimmed();
		const int split = trimmedParams.indexOf(QRegularExpression(QStringLiteral("\\s")));
		const QString channel = split < 0 ? trimmedParams : trimmedParams.left(split);
		const QString fileName = split < 0 ? QString() : QFileInfo(trimmedParams.mid(split + 1).trimmed()).fileName();
		if (!channel.isEmpty() && !fileName.isEmpty())
			descriptor.summary = QStringLiteral("%1 · %2").arg(channel, fileName);
		else if (!channel.isEmpty())
			descriptor.summary = channel;
	}
	else if (commandLower == QStringLiteral("vstplugin"))
	{
		descriptor.type = QStringLiteral("vst");
		descriptor.badge = QStringLiteral("VST");
		descriptor.title = tr("VST Plugin");
		descriptor.color = QStringLiteral("#a855f7");
		descriptor.summary = QFileInfo(parameters).fileName();
		if (descriptor.summary.isEmpty())
			descriptor.summary = parameters;
	}
	else if (commandLower == QStringLiteral("device"))
	{
		descriptor.type = QStringLiteral("device");
		descriptor.badge = QStringLiteral("DEV");
		descriptor.title = tr("Device");
		descriptor.color = QStringLiteral("#64748b");
	}
	else if (commandLower == QStringLiteral("stage"))
	{
		descriptor.type = QStringLiteral("stage");
		descriptor.badge = QStringLiteral("STG");
		descriptor.title = tr("Stage");
		descriptor.color = QStringLiteral("#f97316");
	}
	else if (commandLower == QStringLiteral("loudnesscorrection"))
	{
		descriptor.type = QStringLiteral("loudness");
		descriptor.badge = QStringLiteral("LOUD");
		descriptor.title = tr("Loudness");
		descriptor.color = QStringLiteral("#eab308");
	}
	else if (commandLower == QStringLiteral("if") || commandLower == QStringLiteral("elseif")
		|| commandLower == QStringLiteral("else") || commandLower == QStringLiteral("endif"))
	{
		// The whole If family shares one card type; the badge tells the branch
		// kind apart. The summary is the condition expression as written - for
		// Else/EndIf the engine ignores any text after the colon, and an empty
		// summary is the honest reading (the title already says everything).
		descriptor.type = QStringLiteral("if");
		descriptor.color = QStringLiteral("#f43f5e");
		if (commandLower == QStringLiteral("if"))
		{
			descriptor.badge = QStringLiteral("IF");
			descriptor.title = tr("If");
		}
		else if (commandLower == QStringLiteral("elseif"))
		{
			descriptor.badge = QStringLiteral("ELIF");
			descriptor.title = tr("Else if");
		}
		else if (commandLower == QStringLiteral("else"))
		{
			descriptor.badge = QStringLiteral("ELSE");
			descriptor.title = tr("Else");
		}
		else
		{
			descriptor.badge = QStringLiteral("ENDIF");
			descriptor.title = tr("End if");
		}
	}
	else if (commandLower == QStringLiteral("eval"))
	{
		descriptor.type = QStringLiteral("eval");
		descriptor.badge = QStringLiteral("EVAL");
		descriptor.title = tr("Eval");
		descriptor.color = QStringLiteral("#0ea5e9");
	}

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
		static const struct { const char* prefix; const char* icon; } curves[] = {
			{ "LP", "eq-lowpass" },
			{ "HP", "eq-highpass" },
			{ "BP", "eq-bandpass" },
			{ "LS", "eq-lowshelf" },
			{ "HS", "eq-highshelf" },
			{ "NO", "eq-notch" },
			{ "AP", "eq-allpass" }
		};
		for (const auto& curve : curves)
			if (badge.startsWith(QLatin1String(curve.prefix)))
				return QStringLiteral(":/icons/modern/%1.svg").arg(QLatin1String(curve.icon));
		return QStringLiteral(":/icons/modern/eq-peaking.svg");
	}
	if (type == QStringLiteral("convolution"))
	{
		// The badge splits the siblings: one shared type, two pictograms.
		return badge == QStringLiteral("MCONV")
			? QStringLiteral(":/icons/modern/multi-convolution.svg")
			: QStringLiteral(":/icons/modern/waveform.svg");
	}

	static const struct { const char* type; const char* icon; } commands[] = {
		{ "comment", "comment-bubble" },
		{ "preamp", "preamp-gain" },
		{ "delay", "delay-clock" },
		{ "graphiceq", "graphic-eq" },
		{ "copy", "route-channels" },
		{ "channel", "channel-select" },
		{ "include", "file-include" },
		{ "vst", "plugin" },
		{ "device", "device-speaker" },
		{ "stage", "stage-chain" },
		{ "loudness", "loudness" },
		{ "if", "logic-if" },
		{ "eval", "logic-eval" }
	};
	for (const auto& mapping : commands)
		if (type == QLatin1String(mapping.type))
			return QStringLiteral(":/icons/modern/%1.svg").arg(QLatin1String(mapping.icon));
	return QString();
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
	int channelDepth = 0;
	int ifDepth = 0;
	for (const QString& line : lines)
	{
		bool enabled = !line.trimmed().startsWith('#');
		QString parameters;
		QString command = commandForLine(line, &parameters).toLower();
		FilterCardRowScope scope;
		if (enabled && command == QStringLiteral("channel"))
		{
			// The Channel row itself sits outside the group it opens but stays
			// inside any enclosing If block.
			scope.indent = ifDepth;
			scope.logic = ifDepth;
			QStringList channels = parseChannelList(parameters);
			channelDepth = (channels.isEmpty() || channels.contains(QStringLiteral("ALL"))) ? 0 : 1;
		}
		else if (enabled && command == QStringLiteral("if"))
		{
			scope.indent = channelDepth + ifDepth;
			scope.logic = ifDepth;
			ifDepth++;
		}
		else if (enabled && (command == QStringLiteral("elseif") || command == QStringLiteral("else")))
		{
			scope.indent = channelDepth + qMax(0, ifDepth - 1);
			scope.logic = ifDepth;
		}
		else if (enabled && command == QStringLiteral("endif"))
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
		scopes.append(scope);
	}

	return scopes;
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
