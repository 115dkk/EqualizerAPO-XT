/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	What a command row tells its skin. QtCore-only, so the row-info
	derivation (CommandRowFacts) is testable in EditorLogicTests; ISkin.h
	includes it for the skin hooks that receive it.
*/

#pragma once

#include <QString>

// Identifies a command row for per-command-type chrome decisions.
struct CommandRowInfo
{
	// FilterCardModel descriptor type ("biquad", "include", "vst", "copy", ...).
	QString type;
	// Lower-cased command token ("filter 1", "include", "vstplugin", ...);
	// matches the "filterKind" dynamic property the card row already sets.
	QString command;
	// True when the consulting widget belongs to the frozen legacy row path
	// (FilterTableRow and the .ui-based filter GUIs it hosts).
	bool legacyRow = false;
	bool enabled = true;
	bool selected = false;
	bool focused = false;
	// True while the cursor is over the row. Populated at paint time by
	// CommandRowFrame for ISkin::paintCardChrome; the construction-time hooks
	// (prepareCommandRow) always see false.
	bool hovered = false;
	int depth = 0;
	// Number of If scopes the row lives inside (FilterCardRowScope::logic):
	// ElseIf/Else/EndIf rows count the scope they branch/close, the If head
	// counts only the scopes above its own. Lets a skin draw If-block rails
	// through branch rows and terminate them on the EndIf row, independent of
	// the channel-group indent that depth carries.
	int logicDepth = 0;
	// LANE GEOMETRY, resolved by the row widget that owns the layout.
	//
	// Four skins used to recompute the same three expressions from depth,
	// logicDepth and tokens.channelGroupIndent - the card's left edge, the centre
	// of an indent band, and the "branch and tail rows mount one unit deeper"
	// rule. The arithmetic is not a design decision, and having it in four places
	// meant a change to the indent could move the card face without moving the
	// lanes drawn beside it. A skin now answers what a lane looks like; where it
	// is comes with the row.
	//
	// laneUnit is one indent band's width, laneCount how many bands are drawn to
	// the left of the card face, and cardLeft the x the face starts at. All in
	// row-widget coordinates, which is the space paintScopeGutter paints in.
	int laneUnit = 0;
	int laneCount = 0;
	int cardLeft = 0;

	// Centre of the given indent band, 0-based from the outermost. What a rail or
	// a scope lane is drawn on.
	int laneCenter(int level) const
	{
		return cardLeft - (laneCount - level) * laneUnit + laneUnit / 2;
	}
	// The card row's outer layout margins: where the gutter starts (cardLeft
	// is this plus laneCount bands) and how far the card face sits below the
	// row's top edge.
	static constexpr int cardMarginLeft = 8;
	static constexpr int cardMarginTop = 4;
	// Left edge of the outermost indent band.
	int laneOrigin() const
	{
		return cardLeft - laneCount * laneUnit;
	}

	// SCOPE GEOMETRY, resolved with the lanes above, so the four gutters that
	// draw the If family's scope lanes stop re-deriving it (audit #422 S2).
	//
	// scopeRole is the row's part in an If block: Head is an If line (drawn
	// even outside any scope, since it opens one), Branch an ElseIf/Else,
	// Tail an EndIf, Member any other row inside at least one If scope, and
	// None a row with no If scope to draw. The If lanes are the innermost
	// logicDepth bands; channelLaneCount counts the channel-group bands
	// outside them. A head's own lane is the one at channelLaneCount +
	// logicDepth; a branch, tail or member row's innermost lane is the one
	// before it.
	enum class ScopeRole
	{
		None,
		Head,
		Branch,
		Tail,
		Member,
	};
	ScopeRole scopeRole = ScopeRole::None;
	int channelLaneCount = 0;
	// Vertical centre of the card header (cardMarginTop plus half the skin's
	// row height), in row-widget coordinates: where a lane meets its card.
	// A skin on the pixel grid truncates it to an int.
	double headerCenterY = 0.0;
	// Facts from the analysis engine's most recent configuration load
	// (ConfigLoadTrace), advisory only - they go stale between an edit and
	// the next analysis run. branchState applies to If-family rows:
	// -1 = unknown (no analysis yet), 0 = condition false / dead branch,
	// 1 = branch taken, 2 = not evaluated (chain already satisfied),
	// 3 = evaluation error.
	int branchState = -1;
	// True when a false branch swallowed this line on the last load.
	bool lineSkipped = false;
	// Eval result or the substituted inline-expression parameter text
	// (empty when unknown); valueError marks a parser failure.
	QString evalText;
	bool valueError = false;
	// Why the last analysis run could not use this line's parameters, straight
	// from the factory that owns the command, or empty when it was fine. A row
	// with this set is one the engine skipped, which until now looked exactly
	// like a note somebody typed.
	//
	// No skin paints it yet: a per-skin treatment for it is a design decision and
	// belongs in a round with the gallery gate. The card carries it as a tooltip
	// in the meantime, which is not a skin surface.
	QString parseError;
	// The engine could not prepare this line's filter (it threw while being set
	// up), so the whole configuration was not applied and the previous one keeps
	// playing. Like parseError, the card carries it as a tooltip for now.
	bool setupFailed = false;
	// True when the line's parameters carry inline `expression` segments, so
	// its numbers are decided at load time. Rows without a dynamic-capable
	// editor host the shared raw body; skins extend their raw-body styling
	// to these rows through this flag.
	bool dynamicLine = false;
	// A short fact the body editor posts for the chrome once it has loaded
	// its subject: the VST card posts the loaded ABI ("VST3"/"VST2") through
	// the row's rowFormatTag dynamic property, and CommandRowFrame samples
	// it at paint time (like hovered). Empty until the subject loaded.
	QString formatTag;
};
