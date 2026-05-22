#include "FilterCardModel.h"

#include <QFileInfo>
#include <QRegularExpression>

QString FilterCardModel::compactWhitespace(QString text)
{
	return text.simplified();
}

QString FilterCardModel::commandForLine(const QString& line, QString* parameters)
{
	QString trimmed = line.trimmed();
	if (trimmed.startsWith('#'))
	{
		trimmed = trimmed.mid(1).trimmed();
		if (trimmed.isEmpty())
		{
			if (parameters != nullptr)
				*parameters = line.trimmed();
			return QStringLiteral("#");
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

	if (command == QStringLiteral("#"))
	{
		descriptor.type = QStringLiteral("comment");
		descriptor.badge = QStringLiteral("#");
		descriptor.title = QStringLiteral("Comment");
		descriptor.summary = compactWhitespace(line.trimmed().mid(1));
		descriptor.color = QStringLiteral("#94a3b8");
		return descriptor;
	}

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
	else if (commandLower == QStringLiteral("filter"))
	{
		descriptor.type = QStringLiteral("biquad");
		descriptor.badge = QStringLiteral("BQUAD");
		descriptor.title = QStringLiteral("Biquad");
		descriptor.color = QStringLiteral("#22c55e");

		QRegularExpression typeExpression(QStringLiteral("\\b(PK|LP|HP|LS|HS|NO|AP|BP)\\b"), QRegularExpression::CaseInsensitiveOption);
		QRegularExpressionMatch match = typeExpression.match(parameters);
		if (match.hasMatch())
			descriptor.badge = match.captured(1).toUpper();
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
		QString parameters;
		QString command = commandForLine(line, &parameters).toLower();
		if (command == QStringLiteral("channel"))
		{
			depths.append(0);
			QStringList channels = parseChannelList(parameters);
			currentDepth = (channels.isEmpty() || channels.contains(QStringLiteral("ALL"))) ? 0 : 1;
		}
		else if (command == QStringLiteral("include"))
		{
			depths.append(currentDepth);
			currentDepth = 0;
		}
		else
		{
			depths.append(currentDepth);
		}
	}

	return depths;
}
