/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RoutingFold.h"

#include <cmath>

#include <QSet>

using std::vector;
using std::wstring;

namespace RoutingFold
{
namespace
{
QSet<QString> upperSet(const QStringList& names)
{
	QSet<QString> set;
	for (const QString& name : names)
		set.insert(name.toUpper());
	return set;
}
}

Fold fold(const vector<Assignment>& seeded,
	const vector<wstring>& channelNames,
	const QStringList& pinned, bool expanded,
	const QStringList& fixedInputs)
{
	Fold result;
	const QSet<QString> pinnedUpper = upperSet(pinned);

	QVector<bool> visible(static_cast<int>(seeded.size()), false);
	bool anyContent = false;
	for (int i = 0; i < static_cast<int>(seeded.size()); i++)
	{
		const Assignment& assignment = seeded[i];
		const QString target = QString::fromStdWString(assignment.targetChannel);
		anyContent = anyContent || !assignment.sourceSum.empty();
		visible[i] = expanded || !assignment.sourceSum.empty()
			|| pinnedUpper.contains(target.toUpper());
	}

	// Representative fallback: while the command routes nothing, the first
	// two device channels stand in so there is something to route between.
	// This is keyed on content, not on pins: a freshly added virtual channel
	// (pinned, empty sum) must not chase the representatives away, or it
	// would have no source columns to route from.
	const bool representatives = !expanded && !anyContent;
	if (representatives)
	{
		for (int c = 0; c < static_cast<int>(channelNames.size()) && c < 2; c++)
		{
			const QString channel = QString::fromStdWString(channelNames[c]);
			for (int i = 0; i < static_cast<int>(seeded.size()); i++)
				if (QString::fromStdWString(seeded[i].targetChannel)
					.compare(channel, Qt::CaseInsensitive) == 0)
				{
					visible[i] = true;
					break;
				}
		}
	}

	for (int i = 0; i < visible.size(); i++)
		if (visible[i])
			result.visibleRows.append(i);
	result.hiddenChannels = static_cast<int>(seeded.size()) - result.visibleRows.size();

	// MultiConvolution's source ports are file channels, not device channels.
	// They never participate in the target fold: every IR port remains
	// available while only the output rows collapse.
	if (!fixedInputs.isEmpty())
	{
		result.inputs = fixedInputs;
		return result;
	}

	// Input columns: first-seen across the visible sums, like buildMatrix.
	QSet<QString> seen;
	auto addInput = [&result, &seen](const QString& channel) {
		if (channel.isEmpty() || channel == QLatin1String(" ")
			|| seen.contains(channel.toUpper()))
			return;
		seen.insert(channel.toUpper());
		result.inputs.append(channel);
	};
	for (int row : result.visibleRows)
		for (const Assignment::Summand& summand : seeded[row].sourceSum)
			addInput(QString::fromStdWString(summand.channel));
	// Pinned channels are offered as sources too - a freshly added virtual
	// channel must be routable from, not just to.
	for (const QString& name : pinned)
		addInput(name);
	if (expanded)
	{
		for (const wstring& name : channelNames)
			addInput(QString::fromStdWString(name));
	}
	else if (representatives)
	{
		for (int c = 0; c < static_cast<int>(channelNames.size()) && c < 2; c++)
			addInput(QString::fromStdWString(channelNames[c]));
	}

	return result;
}

QStringList referencedTargets(const vector<Assignment>& assignments)
{
	QStringList targets;
	QSet<QString> seen;
	for (const Assignment& assignment : assignments)
	{
		if (assignment.sourceSum.empty())
			continue;
		const QString target = QString::fromStdWString(assignment.targetChannel);
		if (target.isEmpty() || seen.contains(target.toUpper()))
			continue;
		seen.insert(target.toUpper());
		targets.append(target);
	}
	return targets;
}

bool isValidChannelName(const QString& name)
{
	if (name.isEmpty() || name.size() > 16)
		return false;
	bool hasLetter = false;
	for (const QChar& c : name)
	{
		if (c.isLetter() && c.unicode() < 128)
			hasLetter = true;
		else if (!(c.isDigit() && c.unicode() < 128)
			&& c != QLatin1Char('_') && c != QLatin1Char('-'))
			return false;
	}
	// Purely numeric tokens read as factors or 1-based channel positions in
	// the Copy grammar; a letter keeps the name unambiguous.
	return hasLetter;
}

bool parseFactor(const QString& token, Assignment::Summand& summand)
{
	const QString raw = token.trimmed();
	if (raw.compare(QLatin1String("INV"), Qt::CaseInsensitive) == 0)
	{
		summand.factor = -1.0;
		summand.isDecibel = false;
		return true;
	}
	bool isDecibel = false;
	QString number = raw;
	if (number.endsWith(QLatin1String("db"), Qt::CaseInsensitive))
	{
		isDecibel = true;
		number.chop(2);
		number = number.trimmed();
	}
	// The engine normalizes the decimal comma (audit #250 F015); the editor
	// grammar accepts what the line accepts.
	number.replace(QLatin1Char(','), QLatin1Char('.'));
	bool ok = false;
	const double factor = number.toDouble(&ok);
	if (!ok || !std::isfinite(factor))
		return false;
	summand.factor = factor;
	summand.isDecibel = isDecibel;
	return true;
}

namespace
{
// A channel token the line grammar can carry as a summand: the name
// alphabet of isValidChannelName, plus pure digits (a 1-based position in
// Copy, the only spelling of a port in fixed-port mode).
bool isSourceChannelToken(const QString& token, bool fixedPorts)
{
	if (token.isEmpty() || token.size() > 16)
		return false;
	for (const QChar& c : token)
	{
		const bool digit = c.isDigit() && c.unicode() < 128;
		const bool letter = c.isLetter() && c.unicode() < 128;
		if (fixedPorts ? !digit
			: !(digit || letter || c == QLatin1Char('_') || c == QLatin1Char('-')))
			return false;
	}
	// MultiConvolutionRoutingAdapter reads at most four digits as an index.
	return !fixedPorts || token.size() <= 4;
}
}

bool parseSourceToken(const QString& token, bool fixedPorts, Assignment::Summand& summand)
{
	const QString raw = token.trimmed();
	if (raw.isEmpty())
		return false;

	const int star = raw.indexOf(QLatin1Char('*'));
	if (star >= 0)
	{
		if (raw.indexOf(QLatin1Char('*'), star + 1) >= 0)
			return false;
		const QString factorText = raw.left(star).trimmed();
		const QString channel = raw.mid(star + 1).trimmed();
		Assignment::Summand edited = summand;
		if (!parseFactor(factorText, edited) || !isSourceChannelToken(channel, fixedPorts))
			return false;
		edited.channel = channel.toStdWString();
		summand = edited;
		return true;
	}

	bool bareFactor;
	if (fixedPorts)
	{
		// Ports are decimal: a digit string is a port (or an index too long
		// to be one), anything else can only be a factor.
		bool digits = true;
		for (const QChar& c : raw)
			digits = digits && c.isDigit() && c.unicode() < 128;
		if (digits && !isSourceChannelToken(raw, true))
			return false;
		bareFactor = !digits;
	}
	else
	{
		// The Copy grammar reads "0" and decimals as factors and everything
		// else as a channel; the editor also reads the gain label's own
		// spellings (INV, dB, a sign) as factors.
		bareFactor = raw == QLatin1String("0")
			|| raw.contains(QLatin1Char('.')) || raw.contains(QLatin1Char(','))
			|| raw.endsWith(QLatin1String("db"), Qt::CaseInsensitive)
			|| raw.compare(QLatin1String("INV"), Qt::CaseInsensitive) == 0
			|| raw.startsWith(QLatin1Char('-')) || raw.startsWith(QLatin1Char('+'));
	}
	if (bareFactor)
	{
		Assignment::Summand edited = summand;
		if (!parseFactor(raw, edited))
			return false;
		summand = edited;
		return true;
	}

	if (!isSourceChannelToken(raw, fixedPorts))
		return false;
	summand.channel = raw.toStdWString();
	summand.factor = 1.0;
	summand.isDecibel = false;
	return true;
}

QString sourceToken(const Assignment::Summand& summand)
{
	const QString channel = QString::fromStdWString(summand.channel);
	if (summand.factor == 1.0 && !summand.isDecibel)
		return channel;
	// QString::number(double) is the C "%g" default, the serializer's format;
	// a bare integer factor gains ".0" so the line reads it as a factor.
	QString factor = QString::number(summand.factor);
	if (factor != QLatin1String("0") && !factor.contains(QLatin1Char('.'))
		&& !factor.contains(QLatin1Char('e')))
		factor += QLatin1String(".0");
	if (summand.isDecibel)
		factor += QLatin1String("dB");
	return channel.isEmpty() ? factor : factor + QLatin1Char('*') + channel;
}

bool removeChannel(vector<Assignment>& assignments, const QString& channel)
{
	const QString upper = channel.toUpper();
	bool changed = false;

	for (int i = static_cast<int>(assignments.size()) - 1; i >= 0; i--)
	{
		if (QString::fromStdWString(assignments[i].targetChannel).toUpper() == upper)
		{
			changed = changed || !assignments[i].sourceSum.empty();
			assignments.erase(assignments.begin() + i);
		}
	}

	for (Assignment& assignment : assignments)
	{
		for (int s = static_cast<int>(assignment.sourceSum.size()) - 1; s >= 0; s--)
		{
			if (QString::fromStdWString(assignment.sourceSum[s].channel).toUpper() == upper)
			{
				assignment.sourceSum.erase(assignment.sourceSum.begin() + s);
				changed = true;
			}
		}
	}

	return changed;
}
}
