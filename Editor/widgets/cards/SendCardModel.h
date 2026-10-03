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

#include "filters/SendCommand.h"

// The state behind a Send card: the line's fields, the endpoints it may send
// to, and the card's verdicts on them. QtCore only, so EditorLogicTests pins
// it without a widget. The line grammar itself is SendCommand's.
class SendCardModel
{
	Q_DECLARE_TR_FUNCTIONS(SendCardModel)

public:
	// A render endpoint the line may name, as the Editor's device list knows
	// it.
	struct Endpoint
	{
		// Canonical GUID (SendCommand::canonicalEndpoint).
		QString guid;
		QString name;
		std::vector<std::wstring> channels;
		// The Device Selector's Send option is on for it, so the resident
		// host keeps a stream open there and the received audio plays even
		// when nothing else does.
		bool receives = false;
	};

	// False when the line is not a Send line the card can show; error then
	// says why. An empty parameter string loads as an empty line (the
	// picker's template), which is true.
	bool load(const QString& parameters, QString* error = nullptr);

	// The device whose configuration this is (excluded from the targets) and
	// the render endpoints of the machine.
	void setEndpoints(const QString& ownGuid, const std::vector<Endpoint>& renderEndpoints);
	// The endpoints the picker offers: every render endpoint but this one,
	// plus the line's own target when the machine does not have it.
	const std::vector<Endpoint>& targets() const { return targetList; }
	// Index into targets() of the line's target, -1 when none is written.
	int targetIndex() const;
	void setTarget(const QString& guid);
	const QString& target() const { return endpoint; }
	bool targetKnown() const;
	// The target's channel names; empty when the target is not known.
	std::vector<std::wstring> targetChannels() const;

	const std::vector<Assignment>& assignments() const { return command.assignments; }
	void setAssignments(const std::vector<Assignment>& assignments);

	SendCommand::Mode mode() const { return command.mode; }
	void setMode(SendCommand::Mode mode) { command.mode = mode; }
	bool compensate() const { return command.compensate; }
	void setCompensate(bool on) { command.compensate = on; }

	// The latency as the field shows it: empty for the default, "20" or
	// "13.33" for milliseconds, "960 samples" for samples.
	QString latencyText() const;
	// Takes what the user typed: a number (milliseconds), a number with
	// "ms", or a whole number with "samples". Empty restores the default.
	// False leaves the latency unchanged.
	bool setLatencyText(const QString& text);

	// What the card says under its controls, most important first; empty
	// when there is nothing to say.
	QString warning() const;

	QString serialize() const;

private:
	SendCommand command;
	QString endpoint;
	QString ownGuid;
	std::vector<Endpoint> targetList;
};
