/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
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
	// commands. Commented lines ("# ...") are not card-available under their
	// written key and take the chain, where CommentFilterGUIFactory strips
	// the '#'; the post-chain card retry then gives them the same card editor
	// as their active form (a construction mechanic, not a decision).
	if (modernCards && cardEditorAvailable)
		return RowGuiDecision::CardEditor;

	return RowGuiDecision::LegacyChain;
}
