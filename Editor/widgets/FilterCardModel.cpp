#include "FilterCardModel.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include "filters/FilterFactoryRegistry.h"

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
	const QString normalized = code.toUpper();
	if (normalized == QStringLiteral("PK") || normalized == QStringLiteral("PEQ") || normalized == QStringLiteral("MODAL"))
		return QStringLiteral("Peaking");
	if (normalized == QStringLiteral("LP") || normalized == QStringLiteral("LPQ"))
		return QStringLiteral("Low-pass");
	if (normalized == QStringLiteral("HP") || normalized == QStringLiteral("HPQ"))
		return QStringLiteral("High-pass");
	if (normalized == QStringLiteral("BP"))
		return QStringLiteral("Band-pass");
	if (normalized == QStringLiteral("LS") || normalized == QStringLiteral("LSC"))
		return QStringLiteral("Low-shelf");
	if (normalized == QStringLiteral("HS") || normalized == QStringLiteral("HSC"))
		return QStringLiteral("High-shelf");
	if (normalized == QStringLiteral("NO"))
		return QStringLiteral("Notch");
	if (normalized == QStringLiteral("AP"))
		return QStringLiteral("All-pass");
	return QStringLiteral("Biquad");
}

QString firstCapture(const QRegularExpression& expression, const QString& text)
{
	const QRegularExpressionMatch match = expression.match(text);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

// Reproduces the same labelling the legacy BiQuad GUI shows so the modern card
// agrees with it for the LSC/HSC/LPQ/HPQ/PEQ/Modal variants that the previous
// regex did not recognize.
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

QString FilterCardModel::compactWhitespace(QString text)
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
		descriptor.title = QStringLiteral("Comment");
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
	descriptor.title = command.isEmpty() ? QStringLiteral("Text") : command;
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
		descriptor.title = QStringLiteral("Preamp");
		descriptor.color = QStringLiteral("#f59e0b");
	}
	else if (commandLower == QStringLiteral("delay"))
	{
		descriptor.type = QStringLiteral("delay");
		descriptor.badge = QStringLiteral("DLY");
		descriptor.title = QStringLiteral("Delay");
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
		descriptor.title = QStringLiteral("Biquad");
		descriptor.color = QStringLiteral("#22c55e");

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
	else if (commandLower == QStringLiteral("graphiceq"))
	{
		descriptor.type = QStringLiteral("graphiceq");
		descriptor.badge = QStringLiteral("GEQ");
		descriptor.title = QStringLiteral("Graphic EQ");
		descriptor.color = QStringLiteral("#8b5cf6");

		int bandCount = parameters.count(';') + 1;
		if (!parameters.trimmed().isEmpty())
			descriptor.summary = QStringLiteral("%1 bands").arg(bandCount);
	}
	else if (commandLower == QStringLiteral("copy"))
	{
		descriptor.type = QStringLiteral("copy");
		descriptor.badge = QStringLiteral("CPY");
		descriptor.title = QStringLiteral("Copy");
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
				descriptor.summary = QStringLiteral("%1 steps, %2 virtual").arg(destinations.size()).arg(virtualCount);
			else
				descriptor.summary = QStringLiteral("%1 steps").arg(destinations.size());
		}
	}
	else if (commandLower == QStringLiteral("channel"))
	{
		descriptor.type = QStringLiteral("channel");
		descriptor.badge = QStringLiteral("CH");
		descriptor.title = QStringLiteral("Channel");
		descriptor.color = QStringLiteral("#3b82f6");
		descriptor.routeType = true;
		descriptor.channelBadges = parseChannelList(parameters);
		descriptor.summary = descriptor.channelBadges.join(' ');
	}
	else if (commandLower == QStringLiteral("include"))
	{
		descriptor.type = QStringLiteral("include");
		descriptor.badge = QStringLiteral("INC");
		descriptor.title = QStringLiteral("Include");
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
		descriptor.title = QStringLiteral("Convolution");
		descriptor.color = QStringLiteral("#ec4899");
		QString fileName = QFileInfo(parameters.section(' ', 0, 0)).fileName();
		if (!fileName.isEmpty())
			descriptor.summary = fileName;
	}
	else if (commandLower == QStringLiteral("vstplugin"))
	{
		descriptor.type = QStringLiteral("vst");
		descriptor.badge = QStringLiteral("VST");
		descriptor.title = QStringLiteral("VST Plugin");
		descriptor.color = QStringLiteral("#a855f7");
		descriptor.summary = QFileInfo(parameters).fileName();
		if (descriptor.summary.isEmpty())
			descriptor.summary = parameters;
	}
	else if (commandLower == QStringLiteral("device"))
	{
		descriptor.type = QStringLiteral("device");
		descriptor.badge = QStringLiteral("DEV");
		descriptor.title = QStringLiteral("Device");
		descriptor.color = QStringLiteral("#64748b");
	}
	else if (commandLower == QStringLiteral("stage"))
	{
		descriptor.type = QStringLiteral("stage");
		descriptor.badge = QStringLiteral("STG");
		descriptor.title = QStringLiteral("Stage");
		descriptor.color = QStringLiteral("#f97316");
	}
	else if (commandLower == QStringLiteral("loudnesscorrection"))
	{
		descriptor.type = QStringLiteral("loudness");
		descriptor.badge = QStringLiteral("LOUD");
		descriptor.title = QStringLiteral("Loudness");
		descriptor.color = QStringLiteral("#eab308");
	}

	if (descriptor.summary.isEmpty())
		descriptor.summary = compactWhitespace(line);

	return descriptor;
}

QVector<int> FilterCardModel::calculateDepths(const QList<QString>& lines)
{
	QVector<int> depths;
	depths.reserve(lines.size());

	int currentDepth = 0;
	for (const QString& line : lines)
	{
		bool enabled = !line.trimmed().startsWith('#');
		QString parameters;
		QString command = commandForLine(line, &parameters).toLower();
		if (enabled && command == QStringLiteral("channel"))
		{
			depths.append(0);
			QStringList channels = parseChannelList(parameters);
			currentDepth = (channels.isEmpty() || channels.contains(QStringLiteral("ALL"))) ? 0 : 1;
		}
		else if (command == QStringLiteral("include"))
		{
			depths.append(currentDepth);
		}
		else
		{
			depths.append(currentDepth);
		}
	}

	return depths;
}
