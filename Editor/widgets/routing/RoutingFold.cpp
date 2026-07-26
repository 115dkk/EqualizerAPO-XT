/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "RoutingFold.h"

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
