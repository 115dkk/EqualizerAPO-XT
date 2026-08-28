/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ChannelSelectionModel.h"

#include "filters/ChannelCommand.h"

namespace
{
// The legacy multi-select dialog (ChannelFilterGUIDialog) wrote the standard
// positions in its fixed checkbox order, then the device's non-standard
// channels, then custom names. Serializing in the same order keeps the
// written line byte-identical with what the old flow produced for the same
// selection.
const QString kStandardOrder[] = {
	QStringLiteral("C"), QStringLiteral("L"), QStringLiteral("R"),
	QStringLiteral("SL"), QStringLiteral("SR"), QStringLiteral("RL"),
	QStringLiteral("RR"), QStringLiteral("RC"), QStringLiteral("LFE")
};

bool isStandardName(const QString& name)
{
	for (const QString& standard : kStandardOrder)
		if (name == standard)
			return true;
	return false;
}
}

void ChannelSelectionModel::load(const QString& parameters, const std::vector<std::wstring>& deviceChannels)
{
	chipList.clear();
	deviceNames.clear();
	all = false;

	for (const std::wstring& channel : deviceChannels)
		deviceNames.append(QString::fromStdWString(channel));

	// Device chips in canonical order: standard positions first (dialog
	// checkbox order), then the remaining device channels in device order.
	for (const QString& standard : kStandardOrder)
	{
		if (deviceNames.contains(standard))
		{
			ChannelChip chip;
			chip.name = standard;
			chip.fromDevice = true;
			chipList.append(chip);
		}
	}
	for (const QString& name : deviceNames)
	{
		if (!isStandardName(name))
		{
			ChannelChip chip;
			chip.name = name;
			chip.fromDevice = true;
			chipList.append(chip);
		}
	}

	// Selector tokens come from the grammar owner; ChannelCommand::parse
	// upper-cases and splits on whitespace/commas exactly like the engine.
	ChannelCommand cmd;
	ChannelCommand::parse(L"Channel", parameters.toStdWString(), cmd);
	for (const std::wstring& token : cmd.channels)
	{
		const QString name = QString::fromStdWString(token);
		if (name == QStringLiteral("ALL"))
		{
			all = true;
			continue;
		}

		int index = resolveDeviceChip(name);
		if (index < 0)
			index = chipIndex(name);
		if (index >= 0)
		{
			chipList[index].selected = true;
			continue;
		}

		ChannelChip chip;
		chip.name = name;
		chip.selected = true;
		chipList.append(chip);
	}
}

const QList<ChannelChip>& ChannelSelectionModel::chips() const
{
	return chipList;
}

bool ChannelSelectionModel::allSelected() const
{
	return all;
}

void ChannelSelectionModel::setAllSelected(bool on)
{
	all = on;
}

void ChannelSelectionModel::narrowFromAll()
{
	if (!all)
		return;
	all = false;
	for (ChannelChip& chip : chipList)
		chip.selected = false;
}

void ChannelSelectionModel::toggle(const QString& name)
{
	int index = chipIndex(name);
	if (index < 0)
		return;
	if (all)
	{
		narrowFromAll();
		chipList[index].selected = true;
		return;
	}
	chipList[index].selected = !chipList[index].selected;
}

bool ChannelSelectionModel::addCustom(const QString& name)
{
	const QString token = name.trimmed().toUpper();
	if (token.isEmpty())
		return false;
	// A selector is a single token; whitespace or commas would split into
	// several selectors on the next parse and surprise the user.
	for (const QChar c : token)
		if (c.isSpace() || c == QLatin1Char(','))
			return false;

	narrowFromAll();
	int index = resolveDeviceChip(token);
	if (index < 0)
		index = chipIndex(token);
	if (index >= 0)
	{
		chipList[index].selected = true;
		return true;
	}

	ChannelChip chip;
	chip.name = token;
	chip.selected = true;
	chipList.append(chip);
	return true;
}

QString ChannelSelectionModel::serialize() const
{
	ChannelCommand cmd;
	if (all)
	{
		// The legacy dialog wrote only "ALL" when the all-channels box was
		// checked, dropping any individual selection.
		cmd.channels.push_back(L"ALL");
	}
	else
	{
		for (const ChannelChip& chip : chipList)
			if (chip.selected)
				cmd.channels.push_back(chip.name.toStdWString());
	}
	return QString::fromStdWString(cmd.serialize());
}

int ChannelSelectionModel::chipIndex(const QString& name) const
{
	for (int i = 0; i < chipList.size(); i++)
		if (chipList[i].name == name)
			return i;
	return -1;
}

int ChannelSelectionModel::resolveDeviceChip(const QString& token) const
{
	if (token.isEmpty())
		return -1;

	// Mirrors the engine's selector resolution (ChannelLayout::getChannelIndex):
	// 1-based position numbers index the device order; unmatched names fall
	// back to the historical SL<->RL / SR<->RR / SUB->LFE aliases. The legacy
	// dialog instead numbered its own checkbox order and kept unmatched
	// aliases as custom names; the engine's interpretation is the one that
	// decides what actually plays, so the chips follow it.
	QString resolved;
	if (token[0].isDigit())
	{
		bool ok = false;
		const int number = token.toInt(&ok);
		if (!ok || number < 1 || number > deviceNames.size())
			return -1;
		resolved = deviceNames[number - 1];
	}
	else if (deviceNames.contains(token))
	{
		resolved = token;
	}
	else
	{
		QString alias;
		if (token == QStringLiteral("SL"))
			alias = QStringLiteral("RL");
		else if (token == QStringLiteral("SR"))
			alias = QStringLiteral("RR");
		else if (token == QStringLiteral("RL"))
			alias = QStringLiteral("SL");
		else if (token == QStringLiteral("RR"))
			alias = QStringLiteral("SR");
		else if (token == QStringLiteral("SUB")) // old channel name
			alias = QStringLiteral("LFE");
		if (alias.isEmpty() || !deviceNames.contains(alias))
			return -1;
		resolved = alias;
	}

	for (int i = 0; i < chipList.size(); i++)
		if (chipList[i].fromDevice && chipList[i].name == resolved)
			return i;
	return -1;
}
