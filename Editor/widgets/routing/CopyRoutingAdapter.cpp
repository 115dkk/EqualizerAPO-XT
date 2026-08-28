/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "CopyRoutingAdapter.h"
#include "RoutingFold.h"

#include <QSet>

using std::vector;
using std::wstring;

std::vector<Assignment> CopyRoutingAdapter::parse(const QString& parameters)
{
	// Delegate to the single shared owner of the Copy grammar (parseCopyAssignments
	// in filters/CopyFilter.cpp) so the editor, the runtime factory and the round-trip
	// tests all parse through one routine - the editor and engine stay in
	// lock-step by construction.
	return parseCopyAssignments(parameters.toStdWString());
}

QString CopyRoutingAdapter::serialize(const std::vector<Assignment>& assignments)
{
	// Delegate to the shared serializer so parse(serialize(assignments)) round-trips
	// against the same grammar the engine uses.
	return QString::fromStdWString(serializeCopyAssignments(assignments));
}

bool CopyRoutingAdapter::parseFactorToken(const QString& token, Assignment::Summand& summand)
{
	// One factor grammar for every renderer's gain editor; it lives in
	// RoutingFold (Qt Core only) so EditorLogicTests can pin it.
	return RoutingFold::parseFactor(token, summand);
}

void CopyRoutingAdapter::pinChannel(QStringList& pinnedChannels, const QString& channel)
{
	if (!pinnedChannels.contains(channel, Qt::CaseInsensitive))
		pinnedChannels.append(channel);
}

void CopyRoutingAdapter::ensureTargetChannel(std::vector<Assignment>& assignments,
	QStringList& pinnedChannels, const QString& channel)
{
	for (const Assignment& assignment : assignments)
	{
		if (QString::fromStdWString(assignment.targetChannel).compare(channel, Qt::CaseInsensitive) == 0)
		{
			pinChannel(pinnedChannels, channel);
			return;
		}
	}
	Assignment assignment;
	assignment.targetChannel = channel.toStdWString();
	assignments.push_back(assignment);
	pinChannel(pinnedChannels, channel);
}

bool CopyRoutingAdapter::isVirtualChannel(const QString& channel)
{
	static const QSet<QString> physical = {
		QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
		QStringLiteral("LFE"), QStringLiteral("SUB"),
		QStringLiteral("SL"), QStringLiteral("SR"),
		QStringLiteral("RL"), QStringLiteral("RR"),
		QStringLiteral("BL"), QStringLiteral("BR"),
		QStringLiteral("SBL"), QStringLiteral("SBR"),
		QStringLiteral("RC"), QStringLiteral("FLC"), QStringLiteral("FRC")
	};
	return !physical.contains(channel.toUpper());
}

QString CopyRoutingAdapter::channelColor(const QString& channel)
{
	// Fixed per-channel hues: the cross-skin data ink for channel identity.
	static const QHash<QString, QString> colors = {
		{ QStringLiteral("L"), QStringLiteral("#ef4444") },
		{ QStringLiteral("R"), QStringLiteral("#3b82f6") },
		{ QStringLiteral("C"), QStringLiteral("#22c55e") },
		{ QStringLiteral("LFE"), QStringLiteral("#f59e0b") },
		{ QStringLiteral("SUB"), QStringLiteral("#f59e0b") },
		{ QStringLiteral("SL"), QStringLiteral("#a855f7") },
		{ QStringLiteral("SR"), QStringLiteral("#ec4899") },
		{ QStringLiteral("RL"), QStringLiteral("#f97316") },
		{ QStringLiteral("RR"), QStringLiteral("#06b6d4") },
		{ QStringLiteral("SBL"), QStringLiteral("#8b5cf6") },
		{ QStringLiteral("SBR"), QStringLiteral("#14b8a6") }
	};

	QString key = channel.toUpper();
	if (colors.contains(key))
		return colors.value(key);
	// Virtual channels: derive from their trailing physical-ish suffix or fall
	// back to a neutral slate.
	if (key.startsWith(QLatin1Char('V')) && key.size() > 1)
	{
		const QString base = key.mid(1);
		if (colors.contains(base))
			return colors.value(base);
	}
	return QStringLiteral("#94a3b8");
}

CopyRoutingAdapter::Cell CopyRoutingAdapter::Matrix::cell(int outRow, int inCol) const
{
	return cells.value(indexOf(outRow, inCol), Cell());
}

CopyRoutingAdapter::Matrix CopyRoutingAdapter::buildMatrix(const std::vector<Assignment>& assignments)
{
	return buildMatrix(assignments, std::vector<std::wstring>());
}

CopyRoutingAdapter::Matrix CopyRoutingAdapter::buildMatrix(const std::vector<Assignment>& assignments,
	const std::vector<std::wstring>& channelNames)
{
	Matrix matrix;

	// Inputs in first-seen order across all summands.
	QSet<QString> seenInputs;
	for (const Assignment& assignment : assignments)
	{
		const QString target = QString::fromStdWString(assignment.targetChannel);
		if (target.isEmpty())
			continue;
		matrix.outputs.append(target);
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			const QString channel = QString::fromStdWString(summand.channel);
			if (channel.isEmpty() || channel == QLatin1String(" "))
				continue;
			if (!seenInputs.contains(channel))
			{
				seenInputs.insert(channel);
				matrix.inputs.append(channel);
			}
		}
	}

	// Offer every device channel as an input column, after the channels the
	// command already references. This must happen before the cells are keyed
	// because indexOf() depends on the final column count.
	for (const std::wstring& name : channelNames)
	{
		const QString channel = QString::fromStdWString(name);
		if (channel.isEmpty() || seenInputs.contains(channel))
			continue;
		seenInputs.insert(channel);
		matrix.inputs.append(channel);
	}

	for (int outRow = 0; outRow < matrix.outputs.size(); ++outRow)
	{
		const Assignment& assignment = assignments[outRow];
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			const QString channel = QString::fromStdWString(summand.channel);
			const int inCol = matrix.inputs.indexOf(channel);
			if (inCol < 0)
				continue;
			Cell cell;
			cell.factor = summand.factor;
			cell.isDecibel = summand.isDecibel;
			cell.present = true;
			matrix.cells.insert(matrix.indexOf(outRow, inCol), cell);
		}
	}

	return matrix;
}

CopyRoutingAdapter::Matrix CopyRoutingAdapter::buildMatrix(const std::vector<Assignment>& assignments,
	const QStringList& fixedSources)
{
	Matrix matrix;
	matrix.inputs = fixedSources;

	for (const Assignment& assignment : assignments)
	{
		const QString target = QString::fromStdWString(assignment.targetChannel);
		if (target.isEmpty())
			continue;
		matrix.outputs.append(target);
	}

	for (int outRow = 0; outRow < matrix.outputs.size(); ++outRow)
	{
		const Assignment& assignment = assignments[outRow];
		for (const Assignment::Summand& summand : assignment.sourceSum)
		{
			const QString channel = QString::fromStdWString(summand.channel);
			const int inCol = matrix.inputs.indexOf(channel);
			if (inCol < 0)
				continue;
			Cell cell;
			cell.factor = summand.factor;
			cell.isDecibel = summand.isDecibel;
			cell.present = true;
			matrix.cells.insert(matrix.indexOf(outRow, inCol), cell);
		}
	}

	return matrix;
}

std::vector<Assignment> CopyRoutingAdapter::seedTargets(const std::vector<Assignment>& assignments,
	const std::vector<std::wstring>& channelNames)
{
	vector<Assignment> seeded = assignments;

	QSet<QString> targets;
	for (const Assignment& assignment : assignments)
		targets.insert(QString::fromStdWString(assignment.targetChannel).toUpper());

	for (const wstring& name : channelNames)
	{
		const QString channel = QString::fromStdWString(name);
		if (channel.isEmpty() || targets.contains(channel.toUpper()))
			continue;
		targets.insert(channel.toUpper());
		Assignment assignment;
		assignment.targetChannel = name;
		seeded.push_back(assignment);
	}

	return seeded;
}
