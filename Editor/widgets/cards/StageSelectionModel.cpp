/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
*/

#include "StageSelectionModel.h"

#include "filters/StageCommand.h"

void StageSelectionModel::load(const QString& parameters)
{
	// Parse through the shared codec so the card accepts exactly what the
	// engine accepts (trim, lower-case, split on single spaces).
	StageCommand cmd;
	StageCommand::parse(L"Stage", parameters.toStdWString(), cmd);

	preMix = cmd.contains(StageCommand::preMix);
	postMix = cmd.contains(StageCommand::postMix);
	capture = cmd.contains(StageCommand::capture);

	unknown.clear();
	for (const std::wstring& token : cmd.stages)
	{
		if (token.empty() || token == StageCommand::preMix || token == StageCommand::postMix || token == StageCommand::capture)
			continue;
		unknown.append(QString::fromStdWString(token));
	}
}

bool StageSelectionModel::isSelected(const QString& stage) const
{
	const std::wstring token = stage.toStdWString();
	if (token == StageCommand::preMix)
		return preMix;
	if (token == StageCommand::postMix)
		return postMix;
	if (token == StageCommand::capture)
		return capture;
	return false;
}

void StageSelectionModel::setSelected(const QString& stage, bool on)
{
	const std::wstring token = stage.toStdWString();
	if (token == StageCommand::preMix)
		preMix = on;
	else if (token == StageCommand::postMix)
		postMix = on;
	else if (token == StageCommand::capture)
		capture = on;
}

const QStringList& StageSelectionModel::unknownTokens() const
{
	return unknown;
}

QString StageSelectionModel::serialize() const
{
	// Known stages in the legacy checkbox GUI's order, so the written bytes
	// match what StageFilterGUI produced for the same selection; the unknown
	// tokens follow in written order instead of being dropped.
	StageCommand cmd;
	if (preMix)
		cmd.stages.push_back(StageCommand::preMix);
	if (postMix)
		cmd.stages.push_back(StageCommand::postMix);
	if (capture)
		cmd.stages.push_back(StageCommand::capture);
	for (const QString& token : unknown)
		cmd.stages.push_back(token.toStdWString());

	return QString::fromStdWString(cmd.serialize());
}
