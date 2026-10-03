/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SendCardModel.h"

#include <cmath>

#include <QRegularExpression>

bool SendCardModel::load(const QString& parameters, QString* error)
{
	command = SendCommand();
	endpoint.clear();
	if (parameters.trimmed().isEmpty())
		return true;

	std::wstring reason;
	if (!SendCommand::parse(L"Send", parameters.toStdWString(), command, &reason))
	{
		if (error != nullptr)
			*error = QString::fromStdWString(reason);
		command = SendCommand();
		return false;
	}
	endpoint = QString::fromStdWString(command.endpoint);
	return true;
}

void SendCardModel::setEndpoints(const QString& own, const std::vector<Endpoint>& renderEndpoints)
{
	ownGuid = QString::fromStdWString(SendCommand::canonicalEndpoint(own.toStdWString()));
	targetList.clear();
	for (const Endpoint& candidate : renderEndpoints)
	{
		Endpoint entry = candidate;
		entry.guid = QString::fromStdWString(SendCommand::canonicalEndpoint(candidate.guid.toStdWString()));
		if (entry.guid.isEmpty() || entry.guid == ownGuid)
			continue;
		targetList.push_back(entry);
	}
	// A target this machine does not have stays selectable as written, so
	// opening the card on another computer does not lose the line's target.
	if (!endpoint.isEmpty() && targetIndex() < 0)
	{
		Endpoint unknown;
		unknown.guid = endpoint;
		unknown.name = tr("Unknown endpoint %1").arg(endpoint);
		targetList.push_back(unknown);
	}
}

int SendCardModel::targetIndex() const
{
	for (size_t i = 0; i < targetList.size(); i++)
		if (targetList[i].guid == endpoint)
			return int(i);
	return -1;
}

void SendCardModel::setTarget(const QString& guid)
{
	endpoint = QString::fromStdWString(SendCommand::canonicalEndpoint(guid.toStdWString()));
}

bool SendCardModel::targetKnown() const
{
	const int index = targetIndex();
	return index >= 0 && !targetList[size_t(index)].channels.empty();
}

std::vector<std::wstring> SendCardModel::targetChannels() const
{
	const int index = targetIndex();
	return index < 0 ? std::vector<std::wstring>() : targetList[size_t(index)].channels;
}

void SendCardModel::setAssignments(const std::vector<Assignment>& assignments)
{
	// The routing views keep seeded rows with an empty sum; the line carries
	// only the connected ones, as Copy's serializer does.
	command.assignments.clear();
	for (const Assignment& assignment : assignments)
	{
		Assignment kept;
		kept.targetChannel = assignment.targetChannel;
		for (const Assignment::Summand& summand : assignment.sourceSum)
			if (!summand.channel.empty() && summand.channel != L" ")
				kept.sourceSum.push_back(summand);
		if (!kept.targetChannel.empty() && !kept.sourceSum.empty())
			command.assignments.push_back(kept);
	}
}

QString SendCardModel::latencyText() const
{
	switch (command.latencyUnit)
	{
	case SendCommand::LatencyUnit::Default:
		break;
	case SendCommand::LatencyUnit::Milliseconds:
		return QString::number(command.latency, 'g', 6);
	case SendCommand::LatencyUnit::Samples:
		// The unit is the line's keyword, so the field reads back what it
		// shows in every language.
		return QStringLiteral("%1 samples").arg(QString::number(command.latency, 'f', 0));
	}
	return QString();
}

bool SendCardModel::setLatencyText(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty())
	{
		command.latencyUnit = SendCommand::LatencyUnit::Default;
		command.latency = 0.0;
		return true;
	}

	static const QRegularExpression pattern(
		QStringLiteral("^([0-9]+(?:[.,][0-9]+)?)\\s*(ms|samples?)?$"),
		QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch match = pattern.match(trimmed);
	if (!match.hasMatch())
		return false;

	QString number = match.captured(1);
	number.replace(QLatin1Char(','), QLatin1Char('.'));
	bool ok = false;
	const double value = number.toDouble(&ok);
	if (!ok || !std::isfinite(value) || value <= 0.0)
		return false;

	const bool samples = match.captured(2).startsWith(QLatin1String("sample"), Qt::CaseInsensitive);
	if (samples && value != std::floor(value))
		return false;

	command.latencyUnit = samples ? SendCommand::LatencyUnit::Samples : SendCommand::LatencyUnit::Milliseconds;
	command.latency = value;
	return true;
}

QString SendCardModel::warning() const
{
	if (endpoint.isEmpty())
		return tr("Choose the endpoint to send to.");
	if (!targetKnown())
		return tr("This computer has no playback endpoint %1, so the engine sends nothing.").arg(endpoint);
	if (command.assignments.empty())
		return tr("Connect at least one channel. The engine skips a Send line with no connection.");
	const int index = targetIndex();
	if (index >= 0 && !targetList[size_t(index)].receives)
		return tr("The Send option of %1 is off in the Device Selector. It plays what this line sends only while another program plays on it.")
			.arg(targetList[size_t(index)].name);
	return QString();
}

QString SendCardModel::serialize() const
{
	if (endpoint.isEmpty())
		return QString();
	SendCommand written = command;
	written.endpoint = endpoint.toStdWString();
	return QString::fromStdWString(written.serialize());
}
