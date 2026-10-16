/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FilterRowGuiPolicy.h"

#include "FilterCardModel.h"

RowGuiDecision decideRowGui(bool modernCards, const QString& line,
	const FilterCardDescriptor* descriptor,
	bool routingRendererAvailable, bool cardEditorAvailable)
{
	if (modernCards)
	{
		// A pure comment has no "command: parameters" shape (with or without
		// an inner colon), so it never reaches the factories. In card mode it
		// still gets a real editor; the legacy path stays frozen (raw row).
		if (descriptor->type == QStringLiteral("comment"))
			return RowGuiDecision::CommentCard;

		// Copy's maintained card editor is the skin routing view built
		// directly by FilterCardRow. Dynamic Copy parameters do not open a
		// routing view (opensRoutingView answers false for them): routing
		// editors must not parse and rewrite inline expressions.
		if (FilterCardModel::opensRoutingView(*descriptor) && routingRendererAvailable)
			return RowGuiDecision::SkinRoutingView;
	}

	if (line.indexOf(':') == -1)
		return RowGuiDecision::RawRow;

	// Card editors take precedence over the legacy chain, so constructing a
	// legacy GUI only to replace and delete it never happens for card-covered
	// commands. The caller asks the card registry with rowGuiCardKey, so a
	// commented line ("# Include: a.txt") is card-available exactly when its
	// active form is, and the chain never runs for it.
	if (modernCards && cardEditorAvailable)
		return RowGuiDecision::CardEditor;

	return RowGuiDecision::LegacyChain;
}

RowGuiCardKey rowGuiCardKey(bool modernCards, const QString& line)
{
	RowGuiCardKey key;
	const int pos = line.indexOf(':');
	if (pos == -1)
		return key;

	key.hasColon = true;
	key.command = line.mid(0, pos).trimmed();
	key.parameters = line.mid(pos + 1);
	if (modernCards && key.command.startsWith('#'))
		key.command = key.command.mid(1).trimmed();
	return key;
}
