/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "NewChannelListModel.h"

#include <utility>

namespace
{
QStringList parseNames(const QString& text)
{
	NewChannelCommand cmd;
	NewChannelCommand::parse(L"NewChannel", text.toStdWString(), cmd);
	QStringList names;
	for (const std::wstring& name : cmd.names)
		names.append(QString::fromStdWString(name));
	return names;
}
}

void NewChannelListModel::load(const QString& parameters)
{
	list = parseNames(parameters);
}

void NewChannelListModel::setDeviceChannels(std::vector<std::wstring> channels)
{
	deviceChannels = std::move(channels);
}

NewChannelCommand::Problem NewChannelListModel::problem(const QString& name) const
{
	return NewChannelCommand::judgeName(name.toStdWString(), deviceChannels);
}

bool NewChannelListModel::hasProblems() const
{
	for (const QString& name : list)
		if (problem(name) != NewChannelCommand::Problem::None)
			return true;
	return false;
}

NewChannelListModel::AddOutcome NewChannelListModel::add(const QString& text)
{
	AddOutcome outcome;
	const QStringList typed = parseNames(text);
	for (const QString& name : typed)
	{
		const NewChannelCommand::Problem verdict = problem(name);
		if (verdict != NewChannelCommand::Problem::None)
		{
			outcome.rejected = name;
			outcome.problem = verdict;
			return outcome;
		}
	}
	for (const QString& name : typed)
	{
		if (list.contains(name))
			continue;
		list.append(name);
		outcome.added.append(name);
	}
	return outcome;
}

void NewChannelListModel::remove(const QString& name)
{
	list.removeAll(name);
}

QString NewChannelListModel::serialize() const
{
	return list.join(QLatin1Char(' '));
}

QString NewChannelListModel::problemText(NewChannelCommand::Problem problem)
{
	switch (problem)
	{
	case NewChannelCommand::Problem::None:
		break;
	case NewChannelCommand::Problem::StartsWithDigit:
		return tr("Starts with a digit, which the engine reads as a channel number");
	case NewChannelCommand::Problem::ReservedAll:
		return tr("ALL already means every channel");
	case NewChannelCommand::Problem::SeparatorCharacter:
		return tr("Contains = * + - . or `, which Copy lines read as part of a formula");
	case NewChannelCommand::Problem::DeviceChannel:
		return tr("Already a channel of this device");
	}
	return QString();
}
