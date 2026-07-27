#include "FilterCardModel.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <utility>

#include "filters/ExpressionCommand.h"
#include "filters/FilterFactoryRegistry.h"
#include "filters/BiQuadCommand.h"
#include "filters/HilbertCommand.h"
#include "filters/VelvetCommand.h"

namespace
{
QString middleDotSeparator()
{
	return QStringLiteral(" %1 ").arg(QChar(0x00B7));
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

	if (keyword == QStringLiteral("Preamp"))
	{
		descriptor.type = QStringLiteral("preamp");
		descriptor.badge = QStringLiteral("PRE");
		descriptor.title = tr("Preamp");
		descriptor.color = QStringLiteral("#f59e0b");
	}
	else if (keyword == QStringLiteral("Delay"))
	{
		descriptor.type = QStringLiteral("delay");
		descriptor.badge = QStringLiteral("DLY");
		descriptor.title = tr("Delay");
		descriptor.color = QStringLiteral("#14b8a6");
	}
	else if (keyword == QStringLiteral("Hilbert"))
	{
		descriptor.type = QStringLiteral("hilbert");
		descriptor.badge = QStringLiteral("H90");
		descriptor.title = tr("Hilbert transform");
		descriptor.color = QStringLiteral("#6366f1");
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
		descriptor.type = QStringLiteral("velvet");
		descriptor.badge = QStringLiteral("VEL");
		descriptor.title = tr("Velvet decorrelator");
		descriptor.color = QStringLiteral("#d946ef");
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
		descriptor.type = QStringLiteral("biquad");
		descriptor.badge = QStringLiteral("BQUAD");
		descriptor.title = tr("Biquad");
		descriptor.color = QStringLiteral("#22c55e");

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
		descriptor.type = QStringLiteral("graphiceq");
		descriptor.badge = QStringLiteral("GEQ");
		descriptor.title = tr("Graphic EQ");
		descriptor.color = QStringLiteral("#8b5cf6");

		int bandCount = parameters.count(';') + 1;
		if (!parameters.trimmed().isEmpty())
			descriptor.summary = tr("%1 bands").arg(bandCount);
	}
	else if (keyword == QStringLiteral("Copy"))
	{
		descriptor.type = QStringLiteral("copy");
		descriptor.badge = QStringLiteral("CPY");
		descriptor.title = tr("Copy");
		descriptor.color = QStringLiteral("#06b6d4");
		descriptor.routeType = true;

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
		descriptor.type = QStringLiteral("channel");
		descriptor.badge = QStringLiteral("CH");
		descriptor.title = tr("Channel");
		descriptor.color = QStringLiteral("#3b82f6");
		descriptor.routeType = true;
		descriptor.channelBadges = parseChannelList(parameters);
		descriptor.summary = descriptor.channelBadges.join(' ');
	}
	else if (keyword == QStringLiteral("Include"))
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
	else if (keyword == QStringLiteral("Convolution"))
	{
		descriptor.type = QStringLiteral("convolution");
		descriptor.badge = QStringLiteral("CONV");
		descriptor.title = tr("Convolution");
		descriptor.color = QStringLiteral("#ec4899");
		QString fileName = QFileInfo(parameters.section(' ', 0, 0)).fileName();
		if (!fileName.isEmpty())
			descriptor.summary = fileName;
	}
	else if (keyword == QStringLiteral("MultiConvolution"))
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
		descriptor.type = QStringLiteral("vst");
		descriptor.badge = QStringLiteral("VST");
		descriptor.title = tr("VST Plugin");
		descriptor.color = QStringLiteral("#a855f7");
		descriptor.summary = QFileInfo(parameters).fileName();
		if (descriptor.summary.isEmpty())
			descriptor.summary = parameters;
	}
	else if (keyword == QStringLiteral("Device"))
	{
		descriptor.type = QStringLiteral("device");
		descriptor.badge = QStringLiteral("DEV");
		descriptor.title = tr("Device");
		descriptor.color = QStringLiteral("#64748b");
	}
	else if (keyword == QStringLiteral("Stage"))
	{
		descriptor.type = QStringLiteral("stage");
		descriptor.badge = QStringLiteral("STG");
		descriptor.title = tr("Stage");
		descriptor.color = QStringLiteral("#f97316");
	}
	else if (keyword == QStringLiteral("LoudnessCorrection"))
	{
		descriptor.type = QStringLiteral("loudness");
		descriptor.badge = QStringLiteral("LOUD");
		descriptor.title = tr("Loudness");
		descriptor.color = QStringLiteral("#eab308");
	}
	else if (keyword == QStringLiteral("If") || keyword == QStringLiteral("ElseIf")
		|| keyword == QStringLiteral("Else") || keyword == QStringLiteral("EndIf"))
	{
		// The whole If family shares one card type; the badge tells the branch
		// kind apart. The summary is the condition expression as written - for
		// Else/EndIf the engine ignores any text after the colon, and an empty
		// summary is the honest reading (the title already says everything).
		descriptor.type = QStringLiteral("if");
		descriptor.color = QStringLiteral("#f43f5e");
		if (keyword == QStringLiteral("If"))
		{
			descriptor.badge = QStringLiteral("IF");
			descriptor.title = tr("If");
		}
		else if (keyword == QStringLiteral("ElseIf"))
		{
			descriptor.badge = QStringLiteral("ELIF");
			descriptor.title = tr("Else if");
		}
		else if (keyword == QStringLiteral("Else"))
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
	else if (keyword == QStringLiteral("Eval"))
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
	const QString normalized = command.trimmed().toLower();
	if (normalized == QStringLiteral("#") || normalized == QStringLiteral("comment"))
		return QStringLiteral(":/icons/modern/comment-bubble.svg");
	if (normalized == QStringLiteral("filter"))
	{
		const QString upper = QStringLiteral(" ") + parameters.toUpper() + QStringLiteral(" ");
		static const struct { const char* token; const char* icon; } curves[] = {
			{ " PK ", "eq-peaking" },
			{ " LP ", "eq-lowpass" },
			{ " HP ", "eq-highpass" },
			{ " BP ", "eq-bandpass" },
			{ " LS ", "eq-lowshelf" },
			{ " HS ", "eq-highshelf" },
			{ " NO ", "eq-notch" },
			{ " AP ", "eq-allpass" }
		};
		for (const auto& curve : curves)
			if (upper.contains(QLatin1String(curve.token)))
				return QStringLiteral(":/icons/modern/%1.svg").arg(QLatin1String(curve.icon));
		return QStringLiteral(":/icons/modern/eq-peaking.svg");
	}

	static const struct { const char* command; const char* icon; } commands[] = {
		{ "include", "file-include" },
		{ "convolution", "waveform" },
		{ "multiconvolution", "multi-convolution" },
		{ "vstplugin", "plugin" },
		{ "graphiceq", "graphic-eq" },
		{ "preamp", "preamp-gain" },
		{ "delay", "delay-clock" },
		{ "hilbert", "eq-allpass" },
		{ "velvet", "waveform" },
		{ "device", "device-speaker" },
		{ "channel", "channel-select" },
		{ "stage", "stage-chain" },
		{ "copy", "route-channels" },
		{ "loudnesscorrection", "loudness" },
		{ "if", "logic-if" },
		{ "elseif", "logic-if" },
		{ "else", "logic-if" },
		{ "endif", "logic-if" },
		{ "eval", "logic-eval" }
	};
	for (const auto& mapping : commands)
		if (normalized == QLatin1String(mapping.command))
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
