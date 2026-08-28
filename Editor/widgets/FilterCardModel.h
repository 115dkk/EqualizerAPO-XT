/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>

struct FilterCardDescriptor
{
	QString command;
	QString parameters;
	QString type;
	QString badge;
	QString title;
	QString summary;
	QString color;
	QStringList channelBadges;
	// The enclosing Channel: selection this row runs under (empty when the
	// selection is ALL or there is none). Like logicDepth, this comes from
	// neighbouring-line context (the build plan / calculateScopes), not from
	// the line itself, so the row preserves it across describeLine()
	// re-derivations.
	QStringList scopeChannels;
	int depth = 0;
	// Number of If scopes the row lives inside (the logic axis of depth; see
	// FilterCardRowScope::logic). Not derivable from the line alone, so the
	// build plan assigns it from neighbouring-line context and the row preserves
	// it across describeLine() re-derivations.
	int logicDepth = 0;
	bool enabled = true;
	bool canToggleEnabled = true;
	bool routeType = false;
	bool dynamicLine = false;
};

// Per-row scope answer of calculateScopes(): the indent that drives the left
// margin plus the If-nesting count that skins use to draw block rails. The two
// differ on the If family's own lines: ElseIf/Else/EndIf indent at their block
// head's level (indent = outer scopes) while still belonging to the scope they
// branch/close (logic = outer scopes + 1), so a painted rail can pass through
// branch rows and terminate on the EndIf row.
struct FilterCardRowScope
{
	int indent = 0;
	int logic = 0;
	// The Channel: selection in effect for this row (empty = ALL/none). On a
	// Channel row itself this is already the row's new selection; consumers
	// that badge member rows skip the head, which carries its own badges.
	QStringList channels;
};

// Immutable presentation work prepared once for a modern-card rebuild. The
// descriptor carries the parsed line and the scope carries neighbouring-line
// context; consumers no longer reinterpret the same command independently.
struct FilterCardBuildPlan
{
	FilterCardDescriptor descriptor;
	FilterCardRowScope scope;
};

class FilterCardModel
{
	// describeLine() produces the human-readable card title/summary strings. It is
	// a plain (non-QObject) helper, so it cannot inherit QObject::tr(); this macro
	// gives it a static tr() bound to the "FilterCardModel" translation context.
	Q_DECLARE_TR_FUNCTIONS(FilterCardModel)

public:
	static FilterCardDescriptor describeLine(const QString& line, int depth = 0);
	static QVector<FilterCardBuildPlan> prepareRows(const QList<QString>& lines);
	// The modern stroke pictogram for a card's type badge, keyed by the
	// descriptor (matching the picker tiles). Biquad rows split by their type
	// code so every EQ shape carries its response-curve glyph; an unmapped
	// descriptor (raw text lines) returns empty and the badge falls back to
	// its monogram, so future commands degrade gracefully instead of going
	// blank. Picker entries use commandIconResource below, so the command
	// vocabulary and these descriptor-specific cases stay in one owner.
	static QString badgeIconResource(const QString& type, const QString& badge);
	// Shared command vocabulary used by picker entries and card badges.
	// Parameters are consulted only for the Filter response-curve split.
	static QString commandIconResource(const QString& command, const QString& parameters = QString());
	// Full per-row scope description: indent depth (channel group + If nesting)
	// and the If-nesting count. Commented-out lines never open or close scopes,
	// matching the engine (a '#' line is a comment to the parser too).
	static QVector<FilterCardRowScope> calculateScopes(const QList<QString>& lines);
	// Indent-only projection of calculateScopes(), kept for callers and tests
	// that only need the margin depth.
	static QVector<int> calculateDepths(const QList<QString>& lines);
	static QString commandForLine(const QString& line, QString* parameters);
	// The engine's own answer to "is this key a command the engine runs, and
	// which one": FilterFactoryRegistry::canonicalCommand brought across the Qt
	// boundary, empty when no factory claims the key. Every Editor decision that
	// hinges on a line being a command asks this, so the card model, the card
	// editor registry and the engine cannot end up with three different answers.
	// Two properties carry the weight: only the first whitespace-delimited token
	// is matched (so "Filter 1" is the "Filter" command), and the match is
	// case-sensitive (so a 1.4.2 config's "copy: a note to self" stays a note).
	// The one place this is stricter than the registry is the trailing token:
	// only IIR/BiQuad accept "Filter <n>", so "Channel 2" resolves to nothing
	// here rather than handing an inert line to an editor that would rewrite
	// its key (see the implementation comment).
	static QString canonicalCommand(const QString& key);
	// A line that is a note, not a disabled command; such a line has no
	// "command: parameters" shape, so FilterTable routes it to the comment card.
	static bool isPureCommentLine(const QString& line);
	// True when the parameter text carries inline `expression` segments (the
	// shared InlineExpression lexer decides, so "\`" escapes and empty
	// segments follow the engine exactly). Such a line's numbers are decided
	// at load time; editors that would parse and re-serialize them must stand
	// down or open in dynamic mode, or a knob turn silently destroys the
	// expression.
	static bool hasInlineExpressions(const QString& parameters);
	// The raw-body eligibility rule the five skins used to restate verbatim:
	// bare/unmodelled lines (raw text, the If family, Eval) and dynamic lines
	// host the shared raw editor, so a skin's inline raw styling applies to
	// them. Eligibility, not presence - a dynamic line with a dynamic-capable
	// editor has no raw label to style, which the skins' findChild guards
	// already absorb.
	static bool hostsSharedRawBody(const QString& type, bool dynamicLine);
	// A Copy line opens the skin routing view only while its factors are
	// static: the routing editor parses and re-serializes the parameters, so
	// inline-expression factors must stay on the raw body or the first edit
	// would serialize the expression away. Both deciders (the editor factory
	// gate and the row's body construction) ask this one predicate.
	static bool opensRoutingView(const FilterCardDescriptor& descriptor);

private:
	static QStringList parseChannelList(const QString& text);
	static QString compactWhitespace(const QString& text);
	static bool isDisabledCommandLine(const QString& line);
};
