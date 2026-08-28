/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	StageSelectionModel is the pure selection/serialization logic behind the
	Stage card's chips, kept Qt-widget-free so EditorLogicTests can pin the
	written bytes. It parses through the shared StageCommand codec (the same
	one the engine uses) and serializes known stages in the legacy checkbox
	GUI's canonical order (pre-mix, post-mix, capture). Unlike the legacy GUI,
	tokens outside the vocabulary survive an edit: they are kept in written
	order after the known stages instead of being dropped.
*/

#pragma once

#include <QString>
#include <QStringList>

class StageSelectionModel
{
public:
	void load(const QString& parameters);

	// stage is one of the engine vocabulary tokens ("pre-mix", "post-mix",
	// "capture"); anything else is ignored.
	bool isSelected(const QString& stage) const;
	void setSelected(const QString& stage, bool on);

	// Tokens on the line that are not in the vocabulary (the engine reports
	// them as unknown; they select no stage). Preserved verbatim.
	const QStringList& unknownTokens() const;

	// Canonical parameter text: selected known stages in vocabulary order,
	// then the unknown tokens in their written order. Empty when nothing is
	// selected and no unknown token exists ("Stage:" matches no stage).
	QString serialize() const;

private:
	bool preMix = false;
	bool postMix = false;
	bool capture = false;
	QStringList unknown;
};
