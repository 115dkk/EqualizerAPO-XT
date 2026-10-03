/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <string>
#include <vector>

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include "filters/NewChannelCommand.h"

// The name list behind a NewChannel card, with the engine's verdict on each
// name (NewChannelCommand::judgeName). QtCore only, so EditorLogicTests pins
// it without a widget.
//
// A name the engine would refuse stays in the list as written: the engine
// skips the whole line then, and the card shows which name is the reason
// instead of dropping it from the user's text.
class NewChannelListModel
{
	Q_DECLARE_TR_FUNCTIONS(NewChannelListModel)

public:
	void load(const QString& parameters);
	// The device's own channels: names the line may not take.
	void setDeviceChannels(std::vector<std::wstring> channels);

	const QStringList& names() const { return list; }
	NewChannelCommand::Problem problem(const QString& name) const;
	bool hasProblems() const;

	struct AddOutcome
	{
		// Names newly appended (already listed ones are skipped quietly).
		QStringList added;
		// The first name the engine would refuse; nothing is added then.
		QString rejected;
		NewChannelCommand::Problem problem = NewChannelCommand::Problem::None;
	};
	// Adds the names typed into the card's field, separated as on the
	// line (whitespace or commas) and upper-cased the way the engine reads
	// them. All or nothing: one unusable name adds none of them.
	AddOutcome add(const QString& text);
	void remove(const QString& name);

	QString serialize() const;

	// The card's wording for a problem, a phrase that follows "VC: ";
	// empty for None.
	static QString problemText(NewChannelCommand::Problem problem);

private:
	QStringList list;
	std::vector<std::wstring> deviceChannels;
};
